#ifndef _BSP_LED_STATUS_H_
#define _BSP_LED_STATUS_H_

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 LED 状态机 + 开机查询外机电量 */
esp_err_t LedStatusInit(void);

/* 强制刷新一次（配置保存后可调） */
void LedStatusRefresh(void);

/* 是否处于开机静默期（开机 5 分钟内不告警） */
bool LedIsSilentPeriod(void);

/* 取某外机（1~4）LED 状态机确认的离线/低电量标志 */
bool LedGetOfflineConfirmed(uint8_t idx);
bool LedGetLowbattConfirmed(uint8_t idx);

#ifdef __cplusplus
}
#endif

#endif /* _BSP_LED_STATUS_H_ */