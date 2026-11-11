#ifndef __APP_CHIRP_H__
#define __APP_CHIRP_H__

#include "main.h"
#include "app_config.h"  /* 引入分机地址宏 */

/* ==================== ChirpIoT 无线应用层（原有） ==================== */

/* ChirpIoT 应用层初始化 */
void App_ChirpInit(void);

/* ChirpIoT 应用层任务：主循环中周期调用，处理收到的数据 */
void App_ChirpTask(void);

void AppChirp_OnKeyPressed(void);
void AppChirp_OnPirTriggered(void);
void AppChirp_OnBatterySampleDone(void);
void AppChirp_OnBoot(void);
void AppChirp_OnChargeFull(void);
void AppChirp_RequestRtc(void);
void AppChirp_OnBatteryReportOnly(void);  /* 只上报电量，不请求 RTC */

/* ==================== ChirpIoT 无线帧格式 ====================
 * 帧格式：[LEN][SRC_ADDR_H][SRC_ADDR_L][OPCODE][PAYLOAD...][CHECKSUM]
 *   LEN      = 2 + 1 + payload_len
 *   CHECKSUM = (LEN + SRC_ADDR_H + SRC_ADDR_L + OPCODE + PAYLOAD各字节) & 0xFF
 *   SRC_ADDR = 发送方 ChirpIoT 地址（CW32 = 0x0001，ESP32 = 0x0000）
 *
 * Debug UART 帧格式（本机调试口，不带源地址）：
 *   [LEN][OPCODE][PAYLOAD...]
 */
/* 操作码枚举（与 ESP32 端保持一致） */
typedef enum {
    /* ---- CW32 主动上报 0x5x ---- */
    APP_MSG_KEY_EVENT      = 0x51,  /* 按下 KEY(PA01)，payload: 4B unix timestamp */
    APP_MSG_PIR_EVENT      = 0x52,  /* PIR 事件，payload: 4B unix timestamp */
    APP_MSG_CHARGE_FULL    = 0x53,  /* 电池充满（高→低跳变），payload: 4B unix timestamp */
    APP_MSG_BATTERY_REPORT = 0x5A,  /* 当前电量，payload: 2B mV（如 3520） */
    APP_MSG_REQ_RTC        = 0x5B,  /* 请求下发 RTC 值，payload: 无 */
    APP_MSG_REQ_XTEA_KEY   = 0x5C,  /* 请求下发 xtea_key，payload: 无 */
    APP_MSG_BOOT_EVENT     = 0x5F,  /* 重启/启动，payload: 4B unix timestamp */

    /* ---- ESP32 主动查询/下发 0xAx ---- */
    APP_MSG_QUERY_BATTERY  = 0xAA,  /* 查询电量，payload: 无；CW32 回 0x5A */
    APP_MSG_SET_RTC        = 0xAB,  /* 下发 RTC，payload: 4B unix timestamp */
    APP_MSG_SET_XTEA_KEY   = 0xAC,  /* 下发 xtea_key，payload: 16B */
} AppMsgOp_t;

/* 协议常量 */
#define APP_MSG_PAYLOAD_MAX     16   /* 最大 payload 字节数（xtea_key 16B） */
#define APP_MSG_FRAME_MAX       (2 + APP_MSG_PAYLOAD_MAX)

/* Debug UART 协议层初始化 */
void AppChirp_Init(void);

/*
 * 收到一帧完整报文时调用（由 Debug UART 组帧层调用）
 *   buf = [LEN][OPCODE][PAYLOAD...]，len = 2 + LEN
 *   本函数负责解析并 ASCII 打印，不做业务动作
 */
void AppChirp_OnDebugBytes(const uint8_t *buf, uint16_t len);

#endif /* __APP_CHIRP_H__ */