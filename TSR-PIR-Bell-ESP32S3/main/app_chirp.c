#include "app_chirp.h"
#include "bsp_chirp.h"
#include "bsp_player.h"
#include "bsp_ntp.h"
#include "bsp_led_status.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "APP_CHIRP";

#define APP_CHIRP_UNIX_2020_OFFSET   1577836800UL
#define APP_CHIRP_TZ_OFFSET_SEC      28800UL
#define APP_CHIRP_AUDIO_SILENT       0xFF

static uint32_t s_last_report_ts[4] = {0, 0, 0, 0};
static uint16_t s_battery_mv[4]     = {0, 0, 0, 0};

static uint32_t AppChirp_NowSec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000LL);
}

static void AppChirp_RecordReport(uint16_t src, uint16_t mv)
{
    uint8_t idx = (uint8_t)(src & 0xFF);
    if (idx < 1 || idx > 4) return;

    s_last_report_ts[idx - 1] = AppChirp_NowSec();
    if (mv > 0) s_battery_mv[idx - 1] = mv;
}

static uint8_t AppChirp_GetAudioIdx(uint16_t src, uint8_t is_key)
{
    uint8_t idx = (uint8_t)(src & 0xFF);
    if (idx < 1 || idx > 4) return 0;

    SysConfig_t *cfg = AppConfig_Get();
    const typeof(cfg->doorbells[0]) *db = &cfg->doorbells[idx - 1];

    uint8_t audio = 0;
    switch (db->mode) {
        case 0: audio = is_key ? db->welcome_key_audio : db->welcome_ir_audio; break;
        case 1: audio = is_key ? db->doorbell_key_audio : db->doorbell_ir_audio; break;
        case 2: audio = is_key ? db->alarm_key_audio : db->alarm_ir_audio; break;
        default: return 0;
    }

    if (audio == APP_CHIRP_AUDIO_SILENT) return 0;
    if (audio < 1 || audio > 10) return 0;
    return audio;
}

static void AppChirp_PlayFor(uint16_t src, uint8_t is_key, const char *event_tag)
{
    uint8_t audio = AppChirp_GetAudioIdx(src, is_key);
    if (audio == 0) return;

    uint8_t idx = (uint8_t)(src & 0xFF);
    SysConfig_t *cfg = AppConfig_Get();
    if (idx >= 1 && idx <= 4 && cfg->doorbells[idx - 1].enabled == 0) return;

    char filename[32];
    snprintf(filename, sizeof(filename), "/audio%02u.mp3", audio);
    char path[64];
    snprintf(path, sizeof(path), "/littlefs%s", filename);

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return;

    ESP_LOGI(TAG, "[%s] doorbell %u -> play %s", event_tag, (unsigned)idx, filename);
    PlayerTryPlay(filename, PLAYER_OWNER_CHIRP, src, is_key);
}

static const char* AppChirp_OpName(uint8_t op)
{
    switch (op) {
        case APP_MSG_OFFLINE_EVENT:  return "OFFLINE_EVENT";
        case APP_MSG_KEY_EVENT:      return "KEY_EVENT";
        case APP_MSG_PIR_EVENT:      return "PIR_EVENT";
        case APP_MSG_CHARGE_FULL:    return "CHARGE_FULL";
        case APP_MSG_BATTERY_REPORT: return "BATTERY_REPORT";
        case APP_MSG_REQ_RTC:        return "REQ_RTC";
        case APP_MSG_REQ_XTEA_KEY:   return "REQ_XTEA_KEY";
        case APP_MSG_BOOT_EVENT:     return "BOOT_EVENT";
        case APP_MSG_QUERY_BATTERY:  return "QUERY_BATTERY";
        case APP_MSG_SET_RTC:        return "SET_RTC";
        case APP_MSG_SET_XTEA_KEY:   return "SET_XTEA_KEY";
        default:                     return "UNKNOWN";
    }
}

static void AppChirp_OnOffline(uint16_t src, const uint8_t *pl, uint8_t pl_len)
{
    uint8_t idx = (uint8_t)(src & 0xFF);
    if (idx < 1 || idx > 4) return;
    ESP_LOGI(TAG, "[OFFLINE_EVENT] from 0x%04X, force set offline", src);
    s_last_report_ts[idx - 1] = 0;
}

static void AppChirp_OnKeyEvent(uint16_t src, const uint8_t *pl, uint8_t pl_len)
{
    AppChirp_PlayFor(src, 1, "KEY");
}

static void AppChirp_OnPirEvent(uint16_t src, const uint8_t *pl, uint8_t pl_len)
{
    AppChirp_PlayFor(src, 0, "PIR");
}

static void AppChirp_OnChargeFull(uint16_t src, const uint8_t *pl, uint8_t pl_len)
{
    uint8_t idx = (uint8_t)(src & 0xFF);
    if (idx < 1 || idx > 4) return;

    if (LedIsSilentPeriod()) return;

    SysConfig_t *cfg = AppConfig_Get();
    if (cfg->doorbells[idx - 1].enabled == 0) return;
    if (cfg->doorbells[idx - 1].fullpwr_enabled == 0) return;

    char filename[32];
    snprintf(filename, sizeof(filename), "/fullpwr%02u.mp3", idx);
    char path[64];
    snprintf(path, sizeof(path), "/littlefs%s", filename);

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return;

    PlayerTryPlay(filename, PLAYER_OWNER_CHIRP, src, 2);
}

static void AppChirp_OnBatteryReport(uint16_t src, const uint8_t *pl, uint8_t pl_len)
{
    // 统一在 AppChirp_OnFrame 处理
}

static void AppChirp_OnReqRtc(uint16_t src, const uint8_t *pl, uint8_t pl_len)
{
    AppChirp_SendRtcToNode(src);
}

static void AppChirp_OnReqXteaKey(uint16_t src, const uint8_t *pl, uint8_t pl_len)
{
    // TODO
}

static void AppChirp_OnBootEvent(uint16_t src, const uint8_t *pl, uint8_t pl_len)
{
    AppChirp_SendRtcToNode(src);
}

void AppChirp_OnFrame(const uint8_t *data, uint8_t data_len)
{
    if (data == NULL || data_len < 3) return;

    uint16_t src = ((uint16_t)data[0] << 8) | data[1];
    uint8_t  op  = data[2];
    const uint8_t *pl = &data[3];
    uint8_t  pl_len = (uint8_t)(data_len - 3);

    /* 用 AppChirp_OpName 打一行，消除 unused-function warning */
    ESP_LOGI(TAG, "RX op=%s(0x%02X) from 0x%04X, pl_len=%u",
             AppChirp_OpName(op), op, src, pl_len);

    if (op >= 0x50 && op <= 0x5F) {
        uint16_t mv = 0;
        if (op == APP_MSG_BATTERY_REPORT && pl_len >= 2) {
            mv = ((uint16_t)pl[0] << 8) | pl[1];
        }
        AppChirp_RecordReport(src, mv);
    }

    switch (op) {
        case APP_MSG_OFFLINE_EVENT:  AppChirp_OnOffline(src, pl, pl_len); break;
        case APP_MSG_KEY_EVENT:      AppChirp_OnKeyEvent(src, pl, pl_len); break;
        case APP_MSG_PIR_EVENT:      AppChirp_OnPirEvent(src, pl, pl_len); break;
        case APP_MSG_CHARGE_FULL:    AppChirp_OnChargeFull(src, pl, pl_len); break;
        case APP_MSG_BATTERY_REPORT: AppChirp_OnBatteryReport(src, pl, pl_len); break;
        case APP_MSG_REQ_RTC:        AppChirp_OnReqRtc(src, pl, pl_len); break;
        case APP_MSG_REQ_XTEA_KEY:   AppChirp_OnReqXteaKey(src, pl, pl_len); break;
        case APP_MSG_BOOT_EVENT:     AppChirp_OnBootEvent(src, pl, pl_len); break;
        default: break;
    }
}

/* ===== 通用发送接口：发往指定节点 ===== */
bool AppChirp_SendToNode(uint16_t dst_addr, uint8_t op, const uint8_t *payload, uint8_t pl_len)
{
    uint8_t data[APP_CHIRP_DATA_MAX];
    if ((uint8_t)(3 + pl_len) > sizeof(data)) return false;

    data[0] = (uint8_t)(APP_CHIRP_LOCAL_ADDR >> 8);
    data[1] = (uint8_t)(APP_CHIRP_LOCAL_ADDR & 0xFF);
    data[2] = op;
    if (pl_len > 0 && payload != NULL) memcpy(&data[3], payload, pl_len);

    return ChirpSendFrame(dst_addr, CHIRP_TARGET_NETID, CHIRP_TARGET_CHANNEL, data, (uint8_t)(3 + pl_len));
}

void AppChirp_Init(void) {}

AppChirpDbState_t AppChirp_GetDoorbellState(uint8_t idx)
{
    if (idx < 1 || idx > 4) return APP_CHIRP_DB_DISABLED;

    SysConfig_t *cfg = AppConfig_Get();
    if (cfg->doorbells[idx - 1].enabled == 0) return APP_CHIRP_DB_DISABLED;

    if (LedGetOfflineConfirmed(idx)) return APP_CHIRP_DB_OFFLINE;
    if (LedGetLowbattConfirmed(idx)) return APP_CHIRP_DB_LOWBATT;

    return APP_CHIRP_DB_ONLINE;
}

uint16_t AppChirp_GetDoorbellBattery(uint8_t idx)
{
    if (idx < 1 || idx > 4) return 0;
    return s_battery_mv[idx - 1];
}

uint32_t AppChirp_GetDoorbellLastReport(uint8_t idx)
{
    if (idx < 1 || idx > 4) return 0;
    return s_last_report_ts[idx - 1];
}

bool AppChirp_QueryBattery(uint8_t idx)
{
    if (idx < 1 || idx > 4) return false;

    uint16_t dst = (uint16_t)idx;
    
    // 只发 0xAA 查询电量
    uint8_t data[3] = {
        (uint8_t)(APP_CHIRP_LOCAL_ADDR >> 8),
        (uint8_t)(APP_CHIRP_LOCAL_ADDR & 0xFF),
        APP_MSG_QUERY_BATTERY
    };
    return ChirpSendFrame(dst, CHIRP_TARGET_NETID, CHIRP_TARGET_CHANNEL, data, sizeof(data));
}


/* 封装：给指定分机下发当前 RTC 时间 */
void AppChirp_SendRtcToNode(uint16_t dst_addr)
{
    time_t now = NtpGetTime();
    if (now == 0) {
        ESP_LOGW(TAG, "[SEND_RTC] NTP not synced, cannot send RTC to 0x%04X", dst_addr);
        return;
    }

    uint32_t ts_1970 = (uint32_t)now;
    uint32_t ts_2020 = (ts_1970 >= APP_CHIRP_UNIX_2020_OFFSET) ? (ts_1970 - APP_CHIRP_UNIX_2020_OFFSET) : 0;
    uint32_t ts_2020_local = ts_2020 + APP_CHIRP_TZ_OFFSET_SEC;

    uint8_t payload[4] = {
        (uint8_t)(ts_2020_local >> 24), (uint8_t)(ts_2020_local >> 16),
        (uint8_t)(ts_2020_local >> 8),  (uint8_t)(ts_2020_local & 0xFF)
    };
    
    AppChirp_SendToNode(dst_addr, APP_MSG_SET_RTC, payload, sizeof(payload));
}