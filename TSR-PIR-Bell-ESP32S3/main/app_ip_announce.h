#ifndef _APP_IP_ANNOUNCE_H_
#define _APP_IP_ANNOUNCE_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 IP 播报任务：轮询 WiFi 待播标志，拆 IP 逐位入队播放。
 * 需在 PlayerInit() 之后调用。 */
esp_err_t AppIpAnnounceInit(void);

#ifdef __cplusplus
}
#endif

#endif /* _APP_IP_ANNOUNCE_H_ */