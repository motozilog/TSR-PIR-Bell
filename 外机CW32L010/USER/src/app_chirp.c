/**
 * @file app_chirp.c
 * @brief ChirpIoT 应用层：无线收发（原有） + Debug UART 协议解析（新增）
 */
#include "app_chirp.h"
#include "bsp_chirp.h"
#include "bsp_adc.h"
#include "bsp_rtc.h"
#include <stdio.h>
#include <string.h>
#include "app_config_mgr.h"
/******************************************************************************
 * 内部变量（无线部分，原有）
 ******************************************************************************/
static uint8_t s_rx_pkt[64];
static uint8_t s_tx_pkt[64];
static uint32_t AppChirp_ReadU32BE(const uint8_t *p);

/******************************************************************************
 * ChirpIoT 应用层（原有）
 ******************************************************************************/
void App_ChirpInit(void)
{
    /* 目前无状态，预留后续状态机初始化 */
}


/* 组一帧带源地址的 DATA（不含外层 LEN/CHECKSUM，那是 Chirp_SendFrame 加的）
 * DATA = [SRC_ADDR_H][SRC_ADDR_L][OPCODE][PAYLOAD...]
 * 返回 DATA 长度
 */
static uint8_t AppChirp_BuildData(uint8_t op, const uint8_t *payload, uint8_t pl_len,
                                  uint8_t *out, uint8_t out_max)
{
    if (out == NULL) return 0;
    uint8_t need = (uint8_t)(2 + 1 + pl_len);
    if (out_max < need) return 0;

    out[0] = APP_CHIRP_LOCAL_ADDR_H;
    out[1] = APP_CHIRP_LOCAL_ADDR_L;
    out[2] = op;
    if (pl_len > 0 && payload != NULL) {
        memcpy(&out[3], payload, pl_len);
    }
    return need;
}

/* 发送一帧：自动加源地址，目标从配置读取 */
static bool AppChirp_SendToEsp(uint8_t op, const uint8_t *payload, uint8_t pl_len)
{
    uint8_t data[3 + APP_MSG_PAYLOAD_MAX];
    uint8_t data_len = AppChirp_BuildData(op, payload, pl_len, data, sizeof(data));
    if (data_len == 0) return false;
    
    SysConfig_t *cfg = AppConfig_Get();
    return Chirp_SendFrame(cfg->peer_addr, cfg->netid, cfg->channel, data, data_len);
}

void App_ChirpTask(void)
{
    uint16_t pkt_len = Chirp_GetPacket(s_rx_pkt, sizeof(s_rx_pkt));
    if (pkt_len == 0) return;

    /* 帧格式：[LEN][SRC_H][SRC_L][OPCODE][PAYLOAD...][CHECKSUM]
     * Chirp_GetPacket 返回的是模块吐出的 [LEN][DATA][CHECKSUM]，
     * 即 pkt_len = 1 + LEN + 1 = LEN + 2
     */
    if (pkt_len < 3) return;

    uint8_t len = s_rx_pkt[0];
    if (pkt_len != (uint16_t)(len + 2)) {
        printf("[CHIRP] len mismatch: pkt=%u LEN=%u\r\n", pkt_len, len);
        return;
    }

    /* 校验和 */
    uint8_t sum = 0;
    for (uint16_t i = 0; i < (uint16_t)(1 + len); i++) sum += s_rx_pkt[i];
    if (sum != s_rx_pkt[1 + len]) {
        printf("[CHIRP] checksum bad: calc=%02X recv=%02X\r\n",
               sum, s_rx_pkt[1 + len]);
        return;
    }

    /* LEN 至少 3：[SRC_H][SRC_L][OPCODE] */
    if (len < 3) {
        printf("[CHIRP] LEN too short: %u\r\n", len);
        return;
    }

    uint8_t src_h = s_rx_pkt[1];
    uint8_t src_l = s_rx_pkt[2];
    uint8_t op    = s_rx_pkt[3];
    const uint8_t *pl = &s_rx_pkt[4];
    uint8_t pl_len = (uint8_t)(len - 3);

    uint16_t src = ((uint16_t)src_h << 8) | src_l;
    printf("[CHIRP] RX from 0x%04X op=0x%02X len=%u\r\n", src, op, pl_len);

    switch (op) {
        case APP_MSG_SET_RTC:
            if (pl_len >= 4) {
                uint32_t ts = AppChirp_ReadU32BE(pl);
                RTC_SetUnixTime(ts);
                printf("  SET_RTC -> RTC updated\r\n");
            }
            break;

        case APP_MSG_SET_XTEA_KEY:
            if (pl_len >= 16) {
                /* TODO: 存 xtea_key，XTEA 未实现前先打印 */
                printf("  SET_XTEA_KEY:");
                for (int i = 0; i < 16; i++) printf(" %02X", pl[i]);
                printf("\r\n");
            }
            break;

        case APP_MSG_QUERY_BATTERY: {
            uint16_t mv = BATT_GetLastVoltage_mV();
            uint8_t pl2[2] = { (uint8_t)(mv >> 8), (uint8_t)(mv & 0xFF) };
            AppChirp_SendToEsp(APP_MSG_BATTERY_REPORT, pl2, sizeof(pl2));
            printf("  QUERY_BATTERY -> reply 5A mv=%u\r\n", mv);
            break;
        }

        default:
            printf("  unknown op, payload:");
            for (int i = 0; i < pl_len; i++) printf(" %02X", pl[i]);
            printf("\r\n");
            break;
    }
}

/******************************************************************************
 * Debug UART 协议层（新增）
 ******************************************************************************/

/* 初始化 */
void AppChirp_Init(void)
{
    /* 目前无状态 */
}

/* 操作码 → 名称 */
static const char* AppChirp_OpName(uint8_t op)
{
    switch (op)
    {
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

/* 大端读 32 位 */
static uint32_t AppChirp_ReadU32BE(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* 按 opcode 打印 payload */
static void AppChirp_PrintPayload(uint8_t op, const uint8_t *pl, uint8_t pl_len)
{
    switch (op)
    {
        case APP_MSG_KEY_EVENT:
        case APP_MSG_PIR_EVENT:
        case APP_MSG_BOOT_EVENT:
            if (pl_len >= 4)
            {
                uint32_t ts = AppChirp_ReadU32BE(pl);
                printf("  timestamp = %lu (0x%08lX)\r\n",
                       (unsigned long)ts, (unsigned long)ts);
            }
            else
            {
                printf("  payload too short (need 4, got %u)\r\n", pl_len);
            }
            break;

        case APP_MSG_SET_RTC:
            if (pl_len >= 4)
            {
                uint32_t ts = AppChirp_ReadU32BE(pl);
                printf("  timestamp = %lu (0x%08lX)\r\n",
                       (unsigned long)ts, (unsigned long)ts);

                RTC_SetUnixTime(ts);

                RTC_DateTime_t dt;
                RTC_GetDateTime(&dt);
                printf("  SET_RTC -> RTC updated, now %04u-%02u-%02u %02u:%02u:%02u\r\n",
                       dt.year, dt.month, dt.day,
                       dt.hour, dt.minute, dt.second);
            }
            else
            {
                printf("  payload too short (need 4, got %u)\r\n", pl_len);
            }
            break;

        case APP_MSG_BATTERY_REPORT:
            if (pl_len >= 2)
            {
                uint16_t mv = ((uint16_t)pl[0] << 8) | pl[1];
                printf("  battery = %u mV (%u.%02u V)\r\n",
                       mv, mv / 1000, (mv % 1000) / 10);
            }
            else
            {
                printf("  payload too short (need 2, got %u)\r\n", pl_len);
            }
            break;

        case APP_MSG_SET_XTEA_KEY:
            if (pl_len >= 16)
            {
                printf("  xtea_key =");
                for (int i = 0; i < 16; i++) printf(" %02X", pl[i]);
                printf("\r\n");
            }
            else
            {
                printf("  payload too short (need 16, got %u)\r\n", pl_len);
            }
            break;

				case APP_MSG_QUERY_BATTERY:
						{
								uint16_t raw = BATT_Measure10TimesAvg();
								uint16_t mv  = BATT_GetLastVoltage_mV();
								uint8_t pl[2] = { (uint8_t)(mv >> 8), (uint8_t)(mv & 0xFF) };

								AppChirp_SendToEsp(APP_MSG_BATTERY_REPORT, pl, sizeof(pl));

								RTC_DateTime_t dt;
								RTC_GetDateTime(&dt);

								printf("  QUERY_BATTERY -> raw=%u, %u mV, reply OP=5A SRC=0x%02X%02X "
											 "(%04u-%02u-%02u %02u:%02u:%02u)\r\n",
											 raw, mv,
											 APP_CHIRP_LOCAL_ADDR_H, APP_CHIRP_LOCAL_ADDR_L,
											 dt.year, dt.month, dt.day,
											 dt.hour, dt.minute, dt.second);
						}
						break;
		
        case APP_MSG_REQ_RTC:
        case APP_MSG_REQ_XTEA_KEY:
            printf("  (no payload)\r\n");
            break;

        default:
            printf("  raw payload (%u bytes):", pl_len);
            for (int i = 0; i < pl_len; i++) printf(" %02X", pl[i]);
            printf("\r\n");
            break;
    }
}


/* 收到一帧完整报文 */
void AppChirp_OnDebugBytes(const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len < 2) return;

    uint8_t pl_len = buf[0];          /* LEN = payload 长度 */
    uint8_t op     = buf[1];
    const uint8_t *pl = &buf[2];

    /* 一致性检查 */
    if (len != (uint16_t)(2 + pl_len))
    {
        printf("[APP] FRAME len mismatch: buf len=%u, LEN=%u\r\n",
               len, pl_len);
        return;
    }

    printf("[APP] RX op=0x%02X (%s) len=%u\r\n",
           op, AppChirp_OpName(op), pl_len);

    AppChirp_PrintPayload(op, pl, pl_len);
}

/******************************************************************************
 * 业务：KEY(PA01) 按下 → Chirp 主动上报 0x51 + 4B unix timestamp
 ******************************************************************************/
void AppChirp_OnKeyPressed(void)
{
    uint32_t ts = RTC_GetUnixTime();
    uint8_t pl[4] = {
        (uint8_t)(ts >> 24), (uint8_t)(ts >> 16),
        (uint8_t)(ts >> 8),  (uint8_t)(ts & 0xFF)
    };

    AppChirp_SendToEsp(APP_MSG_KEY_EVENT, pl, sizeof(pl));

    RTC_DateTime_t dt;
    RTC_GetDateTime(&dt);

    printf("[KEY] Pressed -> Chirp TX: SRC=0x%02X%02X OP=51 %02X %02X %02X %02X "
           "(ts=%lu, %04u-%02u-%02u %02u:%02u:%02u)\r\n",
           APP_CHIRP_LOCAL_ADDR_H, APP_CHIRP_LOCAL_ADDR_L,
           pl[0], pl[1], pl[2], pl[3],
           (unsigned long)ts,
           dt.year, dt.month, dt.day,
           dt.hour, dt.minute, dt.second);
}

/******************************************************************************
 * 业务：PIR 触发 → Chirp 主动上报 0x52 + 4B unix timestamp
 ******************************************************************************/
void AppChirp_OnPirTriggered(void)
{
    uint32_t ts = RTC_GetUnixTime();
    uint8_t pl[4] = {
        (uint8_t)(ts >> 24), (uint8_t)(ts >> 16),
        (uint8_t)(ts >> 8),  (uint8_t)(ts & 0xFF)
    };

    AppChirp_SendToEsp(APP_MSG_PIR_EVENT, pl, sizeof(pl));

    RTC_DateTime_t dt;
    RTC_GetDateTime(&dt);

    printf("[PIR] Triggered -> Chirp TX: SRC=0x%02X%02X OP=52 %02X %02X %02X %02X "
           "(ts=%lu, %04u-%02u-%02u %02u:%02u:%02u)\r\n",
           APP_CHIRP_LOCAL_ADDR_H, APP_CHIRP_LOCAL_ADDR_L,
           pl[0], pl[1], pl[2], pl[3],
           (unsigned long)ts,
           dt.year, dt.month, dt.day,
           dt.hour, dt.minute, dt.second);
}

/******************************************************************************
 * 业务：周期 ADC 采样完成（开发 300s / 生产 3600s）→ Chirp 主动上报 0x5A
 *   注意：mV 取 BATT_GetLastVoltage_mV()（异步采样已更新 s_last_raw）
 ******************************************************************************/
void AppChirp_OnBatterySampleDone(void)
{
    uint16_t mv = BATT_GetLastVoltage_mV();
    uint8_t pl[2] = { (uint8_t)(mv >> 8), (uint8_t)(mv & 0xFF) };

    AppChirp_SendToEsp(APP_MSG_BATTERY_REPORT, pl, sizeof(pl));

    RTC_DateTime_t dt;
    RTC_GetDateTime(&dt);

    printf("[BATT] Report -> Chirp TX: SRC=0x%02X%02X OP=5A %02X %02X "
           "(%u mV, %u.%02u V) (%04u-%02u-%02u %02u:%02u:%02u)\r\n",
           APP_CHIRP_LOCAL_ADDR_H, APP_CHIRP_LOCAL_ADDR_L,
           pl[0], pl[1], mv, mv / 1000, (mv % 1000) / 10,
           dt.year, dt.month, dt.day,
           dt.hour, dt.minute, dt.second);

    /* 采样后顺带请求 RTC */
    AppChirp_RequestRtc();
}

/******************************************************************************
 * 业务：主动上报电量（只发 0x5A，不请求 RTC）
 ******************************************************************************/
void AppChirp_OnBatteryReportOnly(void)
{
    uint16_t mv = BATT_GetLastVoltage_mV();
    uint8_t pl[2] = { (uint8_t)(mv >> 8), (uint8_t)(mv & 0xFF) };

    AppChirp_SendToEsp(APP_MSG_BATTERY_REPORT, pl, sizeof(pl));

    RTC_DateTime_t dt;
    RTC_GetDateTime(&dt);

    printf("[BATT] Heartbeat Report -> Chirp TX: SRC=0x%02X%02X OP=5A %02X %02X "
           "(%u mV, %u.%02u V) (%04u-%02u-%02u %02u:%02u:%02u)\r\n",
           APP_CHIRP_LOCAL_ADDR_H, APP_CHIRP_LOCAL_ADDR_L,
           pl[0], pl[1], mv, mv / 1000, (mv % 1000) / 10,
           dt.year, dt.month, dt.day,
           dt.hour, dt.minute, dt.second);
}

/******************************************************************************
 * 业务：CW32 启动/重启 → Chirp 主动上报 0x5F + 4B unix timestamp
 *   注意：此时 RTC 可能是 2020 初值（ESP32 未下发过），ts 会很小，
 *         这是正常的，ESP32 侧据此触发校时
 ******************************************************************************/
void AppChirp_OnBoot(void)
{
    uint32_t ts = RTC_GetUnixTime();
    uint8_t pl[4] = {
        (uint8_t)(ts >> 24), (uint8_t)(ts >> 16),
        (uint8_t)(ts >> 8),  (uint8_t)(ts & 0xFF)
    };

    AppChirp_SendToEsp(APP_MSG_BOOT_EVENT, pl, sizeof(pl));

    RTC_DateTime_t dt;
    RTC_GetDateTime(&dt);

    printf("[BOOT] Chirp TX: SRC=0x%02X%02X OP=5F %02X %02X %02X %02X "
           "(ts=%lu, %04u-%02u-%02u %02u:%02u:%02u)\r\n",
           APP_CHIRP_LOCAL_ADDR_H, APP_CHIRP_LOCAL_ADDR_L,
           pl[0], pl[1], pl[2], pl[3],
           (unsigned long)ts,
           dt.year, dt.month, dt.day,
           dt.hour, dt.minute, dt.second);
}

/******************************************************************************
 * 业务：电池充满（PB02 高→低跳变）→ Chirp 主动上报 0x53 + 4B unix timestamp
 *   只在跳变时发一次，持续低/持续高/低→高都不发
 ******************************************************************************/
void AppChirp_OnChargeFull(void)
{
    uint32_t ts = RTC_GetUnixTime();
    uint8_t pl[4] = {
        (uint8_t)(ts >> 24), (uint8_t)(ts >> 16),
        (uint8_t)(ts >> 8),  (uint8_t)(ts & 0xFF)
    };

    AppChirp_SendToEsp(APP_MSG_CHARGE_FULL, pl, sizeof(pl));

    RTC_DateTime_t dt;
    RTC_GetDateTime(&dt);

    printf("[FULL] Charge full -> Chirp TX: SRC=0x%02X%02X OP=53 %02X %02X %02X %02X "
           "(ts=%lu, %04u-%02u-%02u %02u:%02u:%02u)\r\n",
           APP_CHIRP_LOCAL_ADDR_H, APP_CHIRP_LOCAL_ADDR_L,
           pl[0], pl[1], pl[2], pl[3],
           (unsigned long)ts,
           dt.year, dt.month, dt.day,
           dt.hour, dt.minute, dt.second);
}

/******************************************************************************
 * 业务：请求 ESP32 下发 RTC（0x5B，无 payload）
 *   启动时、每次电池采样时各调一次
 ******************************************************************************/
void AppChirp_RequestRtc(void)
{
    AppChirp_SendToEsp(APP_MSG_REQ_RTC, NULL, 0);

    RTC_DateTime_t dt;
    RTC_GetDateTime(&dt);

    printf("[RTC] Request RTC from ESP32 -> Chirp TX: SRC=0x%02X%02X OP=5B "
           "(%04u-%02u-%02u %02u:%02u:%02u)\r\n",
           APP_CHIRP_LOCAL_ADDR_H, APP_CHIRP_LOCAL_ADDR_L,
           dt.year, dt.month, dt.day,
           dt.hour, dt.minute, dt.second);
}