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

#ifdef __cplusplus
}
#endif

#endif /* _BSP_NTP_H_ */