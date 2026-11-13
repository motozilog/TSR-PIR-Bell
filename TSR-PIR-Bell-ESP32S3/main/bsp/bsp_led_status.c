#include "bsp_led_status.h"
#include "bsp_ws2812.h"
#include "bsp_player.h"
#include "app_config.h"
#include "app_chirp.h"
#include "bsp_wifistation.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "BSP_LED_STATUS";

/* 开机静默期（不告警播报，LED 只显示蓝/白） */
#define LED_STARTUP_SILENT_SEC        300       /* 5 分钟 */

/* 低电量确认时长 */
#if APP_DEV_MODE
#define LED_LOWBATT_CONFIRM_SEC       300       /* 开发：5 分钟 */
#else
#define LED_LOWBATT_CONFIRM_SEC       1860      /* 生产：31 分钟 */
#endif

/* 主动查询时刻（每分钟的秒数） */
#if APP_DEV_MODE
#define LED_QUERY_SEC_1               2         /* :02 */
#define LED_QUERY_SEC_2               32        /* :32 */
#else
#define LED_QUERY_SEC_1               2         /* :02 */
#define LED_QUERY_SEC_2               2         /* 生产只查 :02 */
#endif

/* 离线播报冷却（同一外机 10 分钟内只播一次） */
#define LED_OFFLINE_ALERT_COOLDOWN_SEC  600

typedef enum {
    LED_COLOR_OFF = 0,
    LED_COLOR_WHITE,
    LED_COLOR_BLUE,
    LED_COLOR_BLUE_BLINK,   /* WiFi 断开重连中：蓝闪 */
    LED_COLOR_YELLOW,
    LED_COLOR_RED,
} LedColor_t;

static uint32_t s_boot_sec = 0;
static bool     s_query_sent_once[4] = {false, false, false, false};
static uint32_t s_last_query_minute = 0xFFFFFFFF;
static uint32_t s_last_query_sec    = 0xFFFFFFFF;

static uint32_t s_lowbatt_candidate_sec[4] = {0, 0, 0, 0}; 
static uint8_t  s_lowbatt_confirmed[4]     = {0, 0, 0, 0};  

static uint8_t  s_offline_confirmed[4]     = {0, 0, 0, 0};  
static uint32_t s_last_offline_alert_sec[4]= {0, 0, 0, 0};  

/* 强制刷新标志：LedStatusRefresh 置位，task 下一轮忽略"颜色未变"直接重设 */
static volatile bool s_force_refresh = false;

/* 蓝闪相位：0=灭 1=亮，每轮切换 */
static bool s_blink_on = false;

static uint32_t LedNowSec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000LL);
}

static void LedSetColor(LedColor_t c, bool blink_on)
{
    switch (c) {
        case LED_COLOR_WHITE:  WS2812_SetColor(255, 255, 255); break;
        case LED_COLOR_BLUE:   WS2812_SetColor(0, 0, 255);     break;
        case LED_COLOR_YELLOW: WS2812_SetColor(255, 200, 0);   break;
        case LED_COLOR_RED:    WS2812_SetColor(255, 0, 0);     break;
        case LED_COLOR_BLUE_BLINK:
            if (blink_on) {
                WS2812_SetColor(0, 0, 255);
            } else {
                WS2812_Off();
            }
            break;
        case LED_COLOR_OFF:
        default:               WS2812_Off();                   break;
    }
}

static void LedPlayAlert(const char *filename_fmt, uint16_t src,
                         uint8_t is_key, const char *tag)
{
    char filename[32];
    snprintf(filename, sizeof(filename), filename_fmt, (unsigned)(src & 0xFF));

    char path[64];
    snprintf(path, sizeof(path), "/littlefs%s", filename);

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        ESP_LOGW(TAG, "[%s] doorbell %u audio %s not exist, skip play",
                 tag, (unsigned)(src & 0xFF), filename);
        return;
    }

    ESP_LOGI(TAG, "[%s] doorbell %u -> play %s",
             tag, (unsigned)(src & 0xFF), filename);
    PlayerTryPlay(filename, PLAYER_OWNER_CHIRP, src, is_key);
}

bool LedIsSilentPeriod(void)
{
    uint32_t now_sec = (uint32_t)(esp_timer_get_time() / 1000000LL);
    return (now_sec < LED_STARTUP_SILENT_SEC);
}

static void LedQueryIfDue(uint32_t now_sec)
{
    uint32_t minute = now_sec / 60;
    uint32_t sec    = now_sec % 60;

    bool due = false;
    if (sec == LED_QUERY_SEC_1) {
        due = true;
    }
#if (LED_QUERY_SEC_2 != LED_QUERY_SEC_1)
    else if (sec == LED_QUERY_SEC_2) {
        due = true;
    }
#endif

    if (!due) return;

    if (minute == s_last_query_minute && sec == s_last_query_sec) {
        return;
    }
    s_last_query_minute = minute;
    s_last_query_sec    = sec;

    ESP_LOGI(TAG, "query due at min=%lu sec=%lu",
             (unsigned long)minute, (unsigned long)sec);

    SysConfig_t *cfg = AppConfig_Get();
    for (uint8_t i = 1; i <= 4; i++) {
        if (cfg->doorbells[i - 1].enabled == 0) continue;
        AppChirp_QueryBattery(i);
        s_query_sent_once[i - 1] = true;
    }
}

static void LedUpdateLowbattState(void)
{
    SysConfig_t *cfg = AppConfig_Get();
    uint32_t now_sec = LedNowSec();
    bool silent = (now_sec < LED_STARTUP_SILENT_SEC);

    for (uint8_t i = 1; i <= 4; i++) {
        uint8_t idx = i - 1;

        if (cfg->doorbells[idx].enabled == 0) {
            s_lowbatt_candidate_sec[idx] = 0;
            s_lowbatt_confirmed[idx]     = 0;
            continue;
        }

        uint16_t mv = AppChirp_GetDoorbellBattery(i);

        if (silent) {
            s_lowbatt_candidate_sec[idx] = 0;
            continue;
        }

        if (mv == 0) continue;

        if (s_lowbatt_confirmed[idx]) {
            if (mv > (APP_CHIRP_LOWBATT_MAX_MV + APP_CHIRP_LOWBATT_HYST_MV) ||
                mv < (APP_CHIRP_LOWBATT_MIN_MV - APP_CHIRP_LOWBATT_HYST_MV)) {
                ESP_LOGI(TAG, "[LOWBATT] doorbell %u exit lowbatt (mv=%u)", i, mv);
                s_lowbatt_confirmed[idx]     = 0;
                s_lowbatt_candidate_sec[idx] = 0;
            }
            continue;
        }

        bool in_range = (mv >= APP_CHIRP_LOWBATT_MIN_MV &&
                         mv <= APP_CHIRP_LOWBATT_MAX_MV);

        if (!in_range) {
            if (s_lowbatt_candidate_sec[idx] != 0) {
                ESP_LOGI(TAG, "[LOWBATT] doorbell %u candidate reset (mv=%u out of range)", i, mv);
            }
            s_lowbatt_candidate_sec[idx] = 0;
            continue;
        }

        if (s_lowbatt_candidate_sec[idx] == 0) {
            s_lowbatt_candidate_sec[idx] = now_sec;
            ESP_LOGI(TAG, "[LOWBATT] doorbell %u candidate start (mv=%u)", i, mv);
        } else if ((now_sec - s_lowbatt_candidate_sec[idx]) >= LED_LOWBATT_CONFIRM_SEC) {
            s_lowbatt_confirmed[idx] = 1;
            ESP_LOGI(TAG, "[LOWBATT] doorbell %u confirmed (mv=%u)", i, mv);

            if (cfg->doorbells[idx].lowpwr_enabled) {
                LedPlayAlert("/lowpwr%02u.mp3", i, 3, "LOWBATT");
            }
        }
    }
}

static void LedUpdateOfflineState(void)
{
    SysConfig_t *cfg = AppConfig_Get();
    uint32_t now_sec = LedNowSec();
    bool silent = (now_sec < LED_STARTUP_SILENT_SEC);

    for (uint8_t i = 1; i <= 4; i++) {
        uint8_t idx = i - 1;

        if (cfg->doorbells[idx].enabled == 0) {
            s_offline_confirmed[idx] = 0;
            continue;
        }

        uint32_t last = AppChirp_GetDoorbellLastReport(i);
        bool is_offline = (last == 0) ||
                          ((now_sec - last) > APP_CHIRP_ONLINE_WINDOW_SEC);

        if (silent) {
            continue; 
        }

        if (is_offline && !s_offline_confirmed[idx]) {
            s_offline_confirmed[idx] = 1;

            if (cfg->doorbells[idx].offline_enabled == 0) {
                ESP_LOGI(TAG, "[OFFLINE] doorbell %u offline (alert off)", i);
                continue;
            }
            if ((now_sec - s_last_offline_alert_sec[idx]) <
                LED_OFFLINE_ALERT_COOLDOWN_SEC &&
                s_last_offline_alert_sec[idx] != 0) {
                ESP_LOGI(TAG, "[OFFLINE] doorbell %u offline (cooldown)", i);
                continue;
            }
            s_last_offline_alert_sec[idx] = now_sec;
            LedPlayAlert("/offline%02u.mp3", i, 4, "OFFLINE");
        } else if (!is_offline) {
            if (s_offline_confirmed[idx]) {
                ESP_LOGI(TAG, "[OFFLINE] doorbell %u back online", i);
            }
            s_offline_confirmed[idx] = 0;
        }
    }
}

bool LedGetOfflineConfirmed(uint8_t idx)
{
    if (idx < 1 || idx > 4) return false;
    return (s_offline_confirmed[idx - 1] != 0);
}

bool LedGetLowbattConfirmed(uint8_t idx)
{
    if (idx < 1 || idx > 4) return false;
    return (s_lowbatt_confirmed[idx - 1] != 0);
}

static LedColor_t LedComputeColor(void)
{
    SysConfig_t *cfg = AppConfig_Get();
    uint32_t now_sec = LedNowSec();

    /* 静默期内：只显示蓝/白，不判红黄 */
    if (now_sec < LED_STARTUP_SILENT_SEC) {
        if (cfg->wifi_mode == 1 && !WifiStationIsConnected()) {
            return LED_COLOR_BLUE_BLINK;   /* 重连中：蓝闪 */
        }
        if (cfg->led_normal_on) {
            return LED_COLOR_WHITE;
        }
        return LED_COLOR_OFF;
    }

    for (uint8_t i = 1; i <= 4; i++) {
        if (cfg->doorbells[i - 1].enabled == 0) continue;
        if (s_offline_confirmed[i - 1]) return LED_COLOR_RED;
    }
    for (uint8_t i = 1; i <= 4; i++) {
        if (cfg->doorbells[i - 1].enabled == 0) continue;
        if (s_lowbatt_confirmed[i - 1]) return LED_COLOR_YELLOW;
    }
    if (cfg->wifi_mode == 1 && !WifiStationIsConnected()) {
        return LED_COLOR_BLUE_BLINK;       /* 重连中：蓝闪 */
    }
    if (cfg->led_normal_on) {
        return LED_COLOR_WHITE;
    }
    return LED_COLOR_OFF;
}

static void LedStatusTask(void *args)
{
    (void)args;

    s_boot_sec = LedNowSec();

    SysConfig_t *cfg = AppConfig_Get();
    for (uint8_t i = 1; i <= 4; i++) {
        if (cfg->doorbells[i - 1].enabled == 0) continue;
        AppChirp_QueryBattery(i);
        s_query_sent_once[i - 1] = true;
    }

    LedColor_t last_color = (LedColor_t)-1;

    while (1) {
        uint32_t now_sec = LedNowSec();

        LedQueryIfDue(now_sec);
        LedUpdateLowbattState();
        LedUpdateOfflineState();

        LedColor_t c = LedComputeColor();

        /* 蓝闪需要每轮翻转相位；其它颜色只需在变化时重设 */
        bool need_update = false;
        if (c == LED_COLOR_BLUE_BLINK) {
            s_blink_on = !s_blink_on;
            need_update = true;
        } else if (c != last_color || s_force_refresh) {
            s_blink_on = true;   /* 非闪烁时相位无关，置 true 保持颜色 */
            need_update = true;
        }

        if (need_update) {
            if (s_force_refresh) {
                ESP_LOGI(TAG, "force refresh, LED color=%d", (int)c);
                s_force_refresh = false;
            } else if (c != last_color) {
                ESP_LOGI(TAG, "LED color %d -> %d", (int)last_color, (int)c);
            }
            LedSetColor(c, s_blink_on);
            last_color = c;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t LedStatusInit(void)
{
    SysConfig_t *cfg = AppConfig_Get();
    uint8_t bri = cfg->led_brightness;
    if (bri < 10) bri = 10;
    if (bri > 100) bri = 100;
    WS2812_SetBrightness(bri);

    for (int i = 0; i < 4; i++) {
        s_query_sent_once[i]        = false;
        s_lowbatt_candidate_sec[i]  = 0;
        s_lowbatt_confirmed[i]      = 0;
        s_offline_confirmed[i]      = 0;
        s_last_offline_alert_sec[i] = 0;
    }
    s_last_query_minute = 0xFFFFFFFF;
    s_last_query_sec    = 0xFFFFFFFF;
    s_force_refresh     = false;
    s_blink_on          = true;

    BaseType_t ret = xTaskCreate(LedStatusTask, "led_status_task",
                                 4096, NULL, 4, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "create led_status_task failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "led_status_task started (brightness=%u%%, dev_mode=%d)",
             (unsigned)bri, APP_DEV_MODE);
    return ESP_OK;
}

/* 配置保存后调用：立即应用亮度并触发下一轮强制刷新颜色 */
void LedStatusRefresh(void)
{
    SysConfig_t *cfg = AppConfig_Get();
    uint8_t bri = cfg->led_brightness;
    if (bri < 10) bri = 10;
    if (bri > 100) bri = 100;
    WS2812_SetBrightness(bri);

    s_force_refresh = true;
    ESP_LOGI(TAG, "LedStatusRefresh requested (brightness=%u%%)", (unsigned)bri);
}