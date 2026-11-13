#ifndef _BSP_NTP_H_
#define _BSP_NTP_H_

#include "esp_err.h"
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NTP 服务器 */
#define NTP_SERVER_NAME     "ntp.aliyun.com"

/* 时区：北京时间 UTC+8 */
#define NTP_TIMEZONE        "CST-8"

/* 启动 SNTP 并创建后台守护任务（异步，立即返回） */
esp_err_t NtpInit(void);

/* 是否已同步成功 */
bool NtpIsSynced(void);

/* 取当前系统时间（秒），未同步时返回 0 */
time_t NtpGetTime(void);

/* 用指定服务器测试能否同步（会短暂切换 SNTP 服务器，失败恢复原服务器） */
esp_err_t NtpTestServer(const char *server, uint32_t timeout_ms);

/* 运行时切换 NTP 服务器（保持切换，不恢复原值）。
 * 内部会停 SNTP → 用新 server 重启 → 更新 s_current_server。
 * server 为空或非法返回 ESP_ERR_INVALID_ARG。 */
esp_err_t NtpSetServer(const char *server);

/* 一次性同步测试：停 SNTP → 用 server 启动 → 轮询等同步。
 * 成功：更新 s_current_server，返回 ESP_OK。
 * 失败：返回 ESP_ERR_TIMEOUT，不恢复原服务器（由调用方决定后续）。
 * 用于网络配置保存前试连场景。 */
esp_err_t NtpSyncOnce(const char *server, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* _BSP_NTP_H_ */