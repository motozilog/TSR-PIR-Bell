#include "bsp_ntp.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "app_config.h"
#include "bsp_wifistation.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "BSP_NTP";

static char s_current_server[64] = NTP_SERVER_NAME;
static bool s_inited = false;

/* SNTP 操作互斥锁 */
static SemaphoreHandle_t s_sntp_lock = NULL;

/* NtpSyncOnce 运行标志，供守护任务避让 */
static volatile bool s_sync_once_running = false;

/* 周期：未同步 60s，已同步 3600s */
#define NTP_RETRY_UNSYNCED_SEC   60
#define NTP_REFRESH_SYNCED_SEC   3600

/* 尝试获取当前同步状态 */
static bool NtpCheckSynced(void)
{
    return (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
}

/**
 * @函数说明        启动/重启 SNTP（调用方需持有 s_sntp_lock）
 *
 * 顺序很关键：必须先 stop（若运行中），再 set operatingmode / servername，
 * 最后 init。否则 SNTP 运行中调 sntp_setoperatingmode 会触发断言：
 *   "Operating mode must not be set while SNTP client is running"
 */
static void NtpStartSntpLocked(const char *server)
{
    setenv("TZ", NTP_TIMEZONE, 1);
    tzset();

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, server);
    esp_sntp_init();
}

/* 加锁包装 */
static void NtpStartSntp(const char *server)
{
    if (s_sntp_lock == NULL) {
        NtpStartSntpLocked(server);
        return;
    }
    xSemaphoreTake(s_sntp_lock, portMAX_DELAY);
    NtpStartSntpLocked(server);
    xSemaphoreGive(s_sntp_lock);
}

/* ===== NTP 状态机守护任务 =====
 * 策略：
 *   - WiFi 刚连上（含中途重连）：立即触发一次 SNTP restart
 *   - 未同步：每 60s 触发一次 esp_sntp_restart（无限重试）
 *   - 已同步：每 3600s 触发一次 esp_sntp_restart（防漂移）
 *   - WiFi 断开：停 SNTP，暂停计时
 */
static void NtpDaemonTask(void *args)
{
    (void)args;

    uint32_t next_try_sec = 0;
    bool     wifi_was_connected = false;
    bool     last_synced = false;

    while (1) {
        uint32_t now_sec = (uint32_t)(esp_timer_get_time() / 1000000LL);
        bool wifi_now = WifiStationIsConnected();
        bool synced = NtpCheckSynced();

        /* 1. WiFi 刚连上（含中途重连） */
        if (wifi_now && !wifi_was_connected) {
            wifi_was_connected = true;

            if (s_sync_once_running) {
                ESP_LOGI(TAG, "WiFi connected but NtpSyncOnce in progress, skip daemon trigger");
                next_try_sec = now_sec + (synced ? NTP_REFRESH_SYNCED_SEC
                                                 : NTP_RETRY_UNSYNCED_SEC);
            } else {
                ESP_LOGI(TAG, "WiFi connected, trigger SNTP restart");
                if (s_sntp_lock) xSemaphoreTake(s_sntp_lock, portMAX_DELAY);
                if (esp_sntp_enabled()) {
                    esp_sntp_restart();
                } else {
                    NtpStartSntpLocked(s_current_server);
                }
                if (s_sntp_lock) xSemaphoreGive(s_sntp_lock);

                next_try_sec = now_sec + (synced ? NTP_REFRESH_SYNCED_SEC
                                                 : NTP_RETRY_UNSYNCED_SEC);
            }
        }
        /* 2. WiFi 断开：停 SNTP，暂停计时 */
        else if (!wifi_now && wifi_was_connected) {
            wifi_was_connected = false;
            ESP_LOGW(TAG, "WiFi lost, stopping SNTP and pausing NTP process");
            if (s_sntp_lock) xSemaphoreTake(s_sntp_lock, portMAX_DELAY);
            if (esp_sntp_enabled()) {
                esp_sntp_stop();
            }
            if (s_sntp_lock) xSemaphoreGive(s_sntp_lock);
            continue;
        }

        /* 3. WiFi 正常，按周期触发 */
        if (wifi_now && now_sec >= next_try_sec) {
            if (s_sntp_lock) xSemaphoreTake(s_sntp_lock, portMAX_DELAY);
            if (esp_sntp_enabled()) {
                esp_sntp_restart();
            } else {
                NtpStartSntpLocked(s_current_server);
            }
            if (s_sntp_lock) xSemaphoreGive(s_sntp_lock);

            if (!synced) {
                ESP_LOGW(TAG, "NTP not synced, retry in %ds", NTP_RETRY_UNSYNCED_SEC);
                next_try_sec = now_sec + NTP_RETRY_UNSYNCED_SEC;
            } else {
                ESP_LOGI(TAG, "NTP periodic refresh in %ds", NTP_REFRESH_SYNCED_SEC);
                next_try_sec = now_sec + NTP_REFRESH_SYNCED_SEC;
            }
        }

        /* 同步状态跳变处理 */
        if (synced && !last_synced) {
            time_t now = time(NULL);
            struct tm tm_local;
            localtime_r(&now, &tm_local);
            char buf[64];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_local);
            ESP_LOGI(TAG, "NTP synced successfully: %s (UTC+8)", buf);
            /* 刚同步成功，改为 1 小时周期 */
            next_try_sec = now_sec + NTP_REFRESH_SYNCED_SEC;
        } else if (!synced && last_synced) {
            ESP_LOGW(TAG, "NTP lost sync, switch to 60s retry");
            next_try_sec = now_sec + NTP_RETRY_UNSYNCED_SEC;
        }
        last_synced = synced;

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ===== 对外接口 ===== */

esp_err_t NtpInit(void)
{
    if (s_inited) {
        ESP_LOGW(TAG, "NTP already initialized");
        return ESP_OK;
    }

    if (s_sntp_lock == NULL) {
        s_sntp_lock = xSemaphoreCreateMutex();
        if (s_sntp_lock == NULL) {
            ESP_LOGE(TAG, "create sntp lock failed");
            return ESP_FAIL;
        }
    }

    const char *server = NTP_SERVER_NAME;
    SysConfig_t *cfg = AppConfig_Get();
    if (cfg && cfg->ntp_server[0] != '\0') {
        server = cfg->ntp_server;
    }

    strncpy(s_current_server, server, sizeof(s_current_server) - 1);
    s_current_server[sizeof(s_current_server) - 1] = '\0';

    s_inited = true;
    ESP_LOGI(TAG, "NTP daemon started, server=%s (unsync=%ds, synced=%ds)",
             server, NTP_RETRY_UNSYNCED_SEC, NTP_REFRESH_SYNCED_SEC);

    BaseType_t ret = xTaskCreate(NtpDaemonTask, "ntp_daemon", 3072, NULL, 3, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "create ntp_daemon task failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool NtpIsSynced(void)
{
    return NtpCheckSynced();
}

time_t NtpGetTime(void)
{
    time_t now = time(NULL);
    if (now > 1577836800) {
        return now;
    }
    return 0;
}

esp_err_t NtpTestServer(const char *server, uint32_t timeout_ms)
{
    if (server == NULL || server[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char backup[64];
    strncpy(backup, s_current_server, sizeof(backup) - 1);
    backup[sizeof(backup) - 1] = '\0';

    ESP_LOGI(TAG, "Test NTP server: %s (timeout %u ms)", server, (unsigned)timeout_ms);

    NtpStartSntp(server);

    uint32_t waited = 0;
    while (waited < timeout_ms) {
        if (NtpCheckSynced()) {
            strncpy(s_current_server, server, sizeof(s_current_server) - 1);
            s_current_server[sizeof(s_current_server) - 1] = '\0';
            ESP_LOGI(TAG, "NTP test OK: %s", server);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        waited += 200;
    }

    ESP_LOGW(TAG, "NTP test failed: %s, restore %s", server, backup);
    NtpStartSntp(backup);

    return ESP_ERR_TIMEOUT;
}

esp_err_t NtpSetServer(const char *server)
{
    if (server == NULL || server[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_current_server, server, sizeof(s_current_server) - 1);
    s_current_server[sizeof(s_current_server) - 1] = '\0';

    ESP_LOGI(TAG, "NTP server switched to: %s", s_current_server);

    if (WifiStationIsConnected()) {
        NtpStartSntp(s_current_server);
    } else {
        ESP_LOGW(TAG, "WiFi not connected, NTP will start with new server after reconnect");
    }

    return ESP_OK;
}

esp_err_t NtpSyncOnce(const char *server, uint32_t timeout_ms)
{
    if (server == NULL || server[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "NtpSyncOnce: %s (timeout %u ms)", server, (unsigned)timeout_ms);

    s_sync_once_running = true;

    NtpStartSntp(server);

    uint32_t waited = 0;
    esp_err_t ret = ESP_ERR_TIMEOUT;
    while (waited < timeout_ms) {
        if (NtpCheckSynced()) {
            strncpy(s_current_server, server, sizeof(s_current_server) - 1);
            s_current_server[sizeof(s_current_server) - 1] = '\0';
            ESP_LOGI(TAG, "NtpSyncOnce OK: %s", server);
            ret = ESP_OK;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        waited += 200;
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NtpSyncOnce timeout: %s", server);
    }

    s_sync_once_running = false;
    return ret;
}