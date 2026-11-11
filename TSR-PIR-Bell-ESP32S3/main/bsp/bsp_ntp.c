#include "bsp_ntp.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "app_config.h"
#include "bsp_wifistation.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "BSP_NTP";

static char s_current_server[64] = NTP_SERVER_NAME;
static bool s_inited = false;

/* 尝试获取当前同步状态 */
static bool NtpCheckSynced(void)
{
    return (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
}

/* 检查并重配底层的 SNTP 参数 */
static void NtpStartSntp(const char *server)
{
    setenv("TZ", NTP_TIMEZONE, 1);
    tzset();

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, server);
    
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    esp_sntp_init();
}

/* ===== NTP 状态机守护任务 ===== */
static void NtpDaemonTask(void *args)
{
    (void)args;

    uint32_t next_try_sec = 0;
    uint8_t  retry_count = 0;
    bool     wifi_was_connected = false;

    while (1) {
        uint32_t now_sec = (uint32_t)(esp_timer_get_time() / 1000000LL);
        bool wifi_now = WifiStationIsConnected();
        bool synced = NtpCheckSynced();

        /* 1. WiFi 刚刚连上，初始化重试逻辑并立刻触发一次 */
        if (wifi_now && !wifi_was_connected) {
            wifi_was_connected = true;
            retry_count = 0;
            next_try_sec = now_sec; 
            ESP_LOGI(TAG, "WiFi connected, starting NTP process");
            
            if (!synced) {
                ESP_LOGI(TAG, "Trigger SNTP init");
                NtpStartSntp(s_current_server);
            }
        }
        /* 2. WiFi 断开，重置状态 */
        else if (!wifi_now && wifi_was_connected) {
            wifi_was_connected = false;
            ESP_LOGW(TAG, "WiFi lost, pausing NTP process");
            /* 这里不清空 next_try_sec，等下次连上会重置 */
            continue;
        }

        /* 3. WiFi 正常，但还没同步，判断是否该重试 */
        if (wifi_now && !synced && (now_sec >= next_try_sec)) {
            if (retry_count < 5) {
                retry_count++;
                ESP_LOGW(TAG, "NTP sync pending, retry %d/5 in 60s", retry_count);
                esp_sntp_restart(); // 重新触发底层请求
                next_try_sec = now_sec + 60;
            } else {
                ESP_LOGW(TAG, "NTP sync failed 5 times, next retry in 30 mins");
                esp_sntp_restart();
                next_try_sec = now_sec + 1800;
            }
        }

        /* 4. 已经同步（或者过了 5 次），到达 30 分钟保底检查时间 */
        /* 如果同步了，每 30 分钟检查一次底层状态。如果中途因为什么原因断了，重新触发 */
        if (wifi_now && synced && (now_sec >= next_try_sec)) {
            ESP_LOGI(TAG, "NTP 30min health check (currently synced)");
            next_try_sec = now_sec + 1800;
            /* 这里什么都不做，因为底层 sntp 自己会定期轮询 */
            /* 如果想强制保活，可以调 esp_sntp_restart()，但没必要 */
        }
        
        /* 5. 如果 5 次失败后到了 30 分钟检查点（且还没同步） */
        if (wifi_now && !synced && retry_count >= 5 && (now_sec >= next_try_sec)) {
            ESP_LOGI(TAG, "NTP 30min health check (currently NOT synced), retry");
            esp_sntp_restart();
            next_try_sec = now_sec + 1800;
        }

        /* 如果已经同步，且刚同步成功的那一刻，打条日志 */
        static bool last_synced = false;
        if (synced && !last_synced) {
            time_t now = time(NULL);
            struct tm tm_local;
            localtime_r(&now, &tm_local);
            char buf[64];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_local);
            ESP_LOGI(TAG, "NTP synced successfully: %s (UTC+8)", buf);
            retry_count = 5; // 确保后续走 30 分钟逻辑
            next_try_sec = now_sec + 1800;
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

    /* 从配置读服务器，空则用默认 */
    const char *server = NTP_SERVER_NAME;
    SysConfig_t *cfg = AppConfig_Get();
    if (cfg && cfg->ntp_server[0] != '\0') {
        server = cfg->ntp_server;
    }

    strncpy(s_current_server, server, sizeof(s_current_server) - 1);
    s_current_server[sizeof(s_current_server) - 1] = '\0';

    s_inited = true;
    ESP_LOGI(TAG, "NTP daemon started, server=%s", server);

    /* 创建后台守护任务 */
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
    /* 2020-01-01 00:00:00 对应的 Unix 时间戳是 1577836800。
       只要系统时间大于这个值（比如到了 2026 年），说明 NTP 已经成功对过时了。 */
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

    /* 1. 停掉当前 SNTP */
    esp_sntp_stop();

    /* 2. 用新服务器重新启动 */
    NtpStartSntp(server);

    /* 3. 轮询等待同步 */
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        if (NtpCheckSynced()) {
            strncpy(s_current_server, server, sizeof(s_current_server) - 1);
            ESP_LOGI(TAG, "NTP test OK: %s", server);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        waited += 200;
    }

    /* 4. 超时失败，恢复原服务器 */
    ESP_LOGW(TAG, "NTP test failed: %s, restore %s", server, backup);
    esp_sntp_stop();
    NtpStartSntp(backup);
    strncpy(s_current_server, backup, sizeof(s_current_server) - 1);

    return ESP_ERR_TIMEOUT;
}