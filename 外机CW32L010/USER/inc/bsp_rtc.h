/**
 * @file bsp_rtc.h
 * @brief RTC 模块驱动
 */
#ifndef __BSP_RTC_H__
#define __BSP_RTC_H__

#include "main.h"
#include "cw32l010_rtc.h"
#include "cw32l010_sysctrl.h"

/* ===== 唤醒源定义 ===== */
#define WAKEUP_SRC_NONE        0
#define WAKEUP_SRC_KEY         1
#define WAKEUP_SRC_CHIRP       2
#define WAKEUP_SRC_RTC_TIMER   3   /* 业务唤醒 */
#define WAKEUP_SRC_PIR         4
#define WAKEUP_SRC_FULL        5
#define WAKEUP_SRC_WDT_FEED    6   /* 静默喂狗唤醒 */

typedef struct
{
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} RTC_DateTime_t;

void BSP_RTC_Init(void);
void RTC_OnIntervalIsr(void);
void RTC_SetUnixTime(uint32_t ts);
uint32_t RTC_GetUnixTime(void);
void RTC_GetDateTime(RTC_DateTime_t *dt);

void RTC_SleepEnable(void);
void RTC_WakeupRestore(void);
void RTC_SetAwakeTimer(uint16_t seconds);

#endif /* __BSP_RTC_H__ */