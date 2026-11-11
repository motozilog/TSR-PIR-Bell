#ifndef _APP_CHIRP_H_
#define _APP_CHIRP_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 无线帧格式（与 CW32 一致） ====================
 * [LEN][SRC_ADDR_H][SRC_ADDR_L][OPCODE][PAYLOAD...][CHECKSUM]
 *   LEN      = 2 + 1 + payload_len
 *   CHECKSUM = (LEN + SRC_H + SRC_L + OPCODE + PAYLOAD各字节) & 0xFF
 *
 * 本机地址：ESP32 = 0x0000
 */

#define APP_CHIRP_LOCAL_ADDR      0x0000

/* 操作码（与 CW32 app_chirp.h 一致） */
typedef enum {
    /* CW32 主动上报 0x5x */
    APP_MSG_OFFLINE_EVENT  = 0x50,
    APP_MSG_KEY_EVENT      = 0x51,
    APP_MSG_PIR_EVENT      = 0x52,
    APP_MSG_CHARGE_FULL    = 0x53,
    APP_MSG_BATTERY_REPORT = 0x5A,
    APP_MSG_REQ_RTC        = 0x5B,
    APP_MSG_REQ_XTEA_KEY   = 0x5C,
    APP_MSG_BOOT_EVENT     = 0x5F,

    /* ESP32 主动查询/下发 0xAx */
    APP_MSG_QUERY_BATTERY  = 0xAA,
    APP_MSG_SET_RTC        = 0xAB,
    APP_MSG_SET_XTEA_KEY   = 0xAC,
} AppMsgOp_t;

/* 帧常量 */
#define APP_CHIRP_PAYLOAD_MAX   16
#define APP_CHIRP_DATA_MAX      (2 + 1 + APP_CHIRP_PAYLOAD_MAX)  /* SRC+OP+payload */

/* ===== 外机状态判定宏（供 app_chirp.c / bsp_led_status.c 共用） ===== */

/* 在线判定窗口：多久没收到 0x50~0x5F 上报就判离线 */
#if APP_DEV_MODE
#define APP_CHIRP_ONLINE_WINDOW_SEC   300       /* 开发：5 分钟 */
#else
#define APP_CHIRP_ONLINE_WINDOW_SEC   3660      /* 生产：61 分钟 */
#endif

/* 低电量区间 */
#define APP_CHIRP_LOWBATT_MIN_MV      3000
#define APP_CHIRP_LOWBATT_MAX_MV      3500

/* 低电量防抖迟滞：确认后 ±100mV 内都算低电量（2900~3600mV） */
#define APP_CHIRP_LOWBATT_HYST_MV     100

/* 初始化 */
void AppChirp_Init(void);

/* 收到一帧 DATA（不含外层 LEN/CHECKSUM），由 bsp_chirp 解析后调用
 * data = [SRC_H][SRC_L][OPCODE][PAYLOAD...]，data_len = 3 + payload_len
 */
void AppChirp_OnFrame(const uint8_t *data, uint8_t data_len);

/* 发送一帧（自动加本机源地址），发往指定目标节点 */
bool AppChirp_SendToNode(uint16_t dst_addr, uint8_t op, const uint8_t *payload, uint8_t pl_len);

/* 外机状态枚举 */
typedef enum {
    APP_CHIRP_DB_DISABLED = 0,   /* 未启用 */
    APP_CHIRP_DB_OFFLINE  = 1,   /* 离线 */
    APP_CHIRP_DB_LOWBATT  = 2,   /* 低电量（在线） */
    APP_CHIRP_DB_ONLINE   = 3,   /* 在线 */
} AppChirpDbState_t;

/* 取某外机（1~4）当前状态；idx 越界返回 DISABLED */
AppChirpDbState_t AppChirp_GetDoorbellState(uint8_t idx);

/* 取某外机最近一次电量 mV；0 表示未知 */
uint16_t AppChirp_GetDoorbellBattery(uint8_t idx);

/* 取某外机最近一次上报时间（开机秒）；0 表示从未上报 */
uint32_t AppChirp_GetDoorbellLastReport(uint8_t idx);

/* 向下发 0xAA 查询指定外机（1~4）电量，只发不等待 */
bool AppChirp_QueryBattery(uint8_t idx);


/* 封装：给指定分机下发当前 RTC 时间 */
void AppChirp_SendRtcToNode(uint16_t dst_addr);

#ifdef __cplusplus
}
#endif

#endif /* _APP_CHIRP_H_ */