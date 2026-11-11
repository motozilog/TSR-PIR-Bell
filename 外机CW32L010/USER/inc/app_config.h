#ifndef _APP_CONFIG_H_
#define _APP_CONFIG_H_

/* ===== 全局配置 ===== */

/* 开发者模式：
 *   0 = 生产模式（默认进 WOR 模式，发射功率保持出厂默认 22dBm）
 *   1 = 开发模式（默认进传输模式，发射功率设为最低 14dBm）
 */
#define APP_DEV_MODE        1

/* ===== 定时唤醒参数配置 ===== */
/* 开发模式：每 30 秒唤醒，单次最大休眠兜底加 10 秒（40 秒） */
#define UTILS_DEV_INTERVAL_SEC        30
#define UTILS_DEV_MAX_SLEEP_SEC       (UTILS_DEV_INTERVAL_SEC + 10)

/* 生产模式：每 15 分钟唤醒，单次最大休眠兜底加 1 分钟（16 分钟） */
#define UTILS_PROD_INTERVAL_MIN       15
#define UTILS_PROD_MAX_SLEEP_SEC      ((UTILS_PROD_INTERVAL_MIN * 60) + 60)

/* ===== 分机地址配置 ===== */
/* 本机 ChirpIoT 地址（CW32 室外机 = 0x0001） */
#define APP_CHIRP_LOCAL_ADDR_H   0x00
#define APP_CHIRP_LOCAL_ADDR_L   0x02

#endif