/**
 * @file bsp_rtc.c
 * @brief RTC 模块实现
 *        时钟源：LSI（内部 32K RC）
 *        周期：1s 中断
 *        开发模式：1s 触发一次 ADC
 *        生产模式：累加 3600 次（1h）触发一次
 */
#include "bsp_rtc.h"
#include "bsp_adc.h"
#include "utils.h"
#include "app_config.h"

/* 生产模式下的秒计数（1s 中断里累加） */
static volatile uint32_t s_sec_count = 0;

/******************************************************************************
 * 对外接口
 ******************************************************************************/

void BSP_RTC_Init(void)
{
    RTC_InitTypeDef rtc = {0};

    /* 1. 使能 LSI（32K RC） */
    SYSCTRL_LSI_Enable();

    /* 2. 时钟源 = LSI */
    rtc.RTC_ClockSource = RTC_RTCCLK_FROM_LSI;

    /* 3. 初始时间：2020-01-01 00:00:00 */
    rtc.DateStruct.Year  = RTC_BinToBCD(20);
    rtc.DateStruct.Month = RTC_BinToBCD(1);
    rtc.DateStruct.Day   = RTC_BinToBCD(1);
    rtc.DateStruct.Week  = RTC_Weekday_Wednesday;

    rtc.TimeStruct.Hour   = RTC_BinToBCD(0);
    rtc.TimeStruct.Minute = RTC_BinToBCD(0);
    rtc.TimeStruct.Second = RTC_BinToBCD(0);
    rtc.TimeStruct.H24    = RTC_HOUR24;
    rtc.TimeStruct.AMPM   = RTC_H12_AM;

    RTC_Init(&rtc);   /* 调用 CW32 库的 RTC_Init：配时钟源 + 设时间 + 启动 */

    /* 4. 1 秒周期中断 */
    RTC_SetInterval(RTC_INTERVAL_EVERY_1S);
    RTC_ITConfig(RTC_IT_INTERVAL, ENABLE);

    /* 5. NVIC */
    NVIC_SetPriority(RTC_IRQn, 2);
    NVIC_EnableIRQ(RTC_IRQn);
}

void RTC_OnIntervalIsr(void)
{
    /* RTC 硬件自己走时，这里只做节拍 + 触发 ADC */

#if APP_DEV_MODE
    /* 开发模式：累加 30 次（30s）触发一次 ADC */
    s_sec_count++;
    if (s_sec_count >= 30)
    {
        s_sec_count = 0;
        BATT_StartConversion();
    }
#else
    /* 生产模式：累加 3600 次（1h）才触发 */
    s_sec_count++;
    if (s_sec_count >= 3600)
    {
        s_sec_count = 0;
        BATT_StartConversion();
    }
#endif
}

void RTC_SetUnixTime(uint32_t ts)
{
    uint16_t year;
    uint8_t  month, day, hour, minute, second;
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};

    Utils_UnixToDateTime(ts, &year, &month, &day, &hour, &minute, &second);

    /* RTC 年寄存器只存 2 位 BCD，所以传 year % 100 */
    date.Year  = RTC_BinToBCD((uint8_t)(year % 100));
    date.Month = RTC_BinToBCD(month);
    date.Day   = RTC_BinToBCD(day);
    {
        /* 2020-01-01 是周三(3)，往后每天 +1，模 7 */
        uint32_t days = ts / 86400UL;
        date.Week = (uint8_t)((3 + days) % 7);
    }

    time.Hour   = RTC_BinToBCD(hour);
    time.Minute = RTC_BinToBCD(minute);
    time.Second = RTC_BinToBCD(second);
    time.H24    = RTC_HOUR24;
    time.AMPM   = RTC_H12_AM;

    RTC_SetDate(&date);
    RTC_SetTime(&time);
}

uint32_t RTC_GetUnixTime(void)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    uint16_t year;
    uint8_t  month, day, hour, minute, second;

    RTC_GetDate(&date);
    RTC_GetTime(&time);

    /* RTC 存的是 BCD，转二进制；年份是 2 位（20~99），加 2000 */
    year   = 2000 + RTC_BCDToBin(date.Year);
    month  = RTC_BCDToBin(date.Month);
    day    = RTC_BCDToBin(date.Day);
    hour   = RTC_BCDToBin(time.Hour);
    minute = RTC_BCDToBin(time.Minute);
    second = RTC_BCDToBin(time.Second);

    return Utils_DateTimeToUnix(year, month, day, hour, minute, second);
}

void RTC_GetDateTime(RTC_DateTime_t *dt)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};

    RTC_GetDate(&date);
    RTC_GetTime(&time);

    dt->year   = 2000 + RTC_BCDToBin(date.Year);
    dt->month  = RTC_BCDToBin(date.Month);
    dt->day    = RTC_BCDToBin(date.Day);
    dt->hour   = RTC_BCDToBin(time.Hour);
    dt->minute = RTC_BCDToBin(time.Minute);
    dt->second = RTC_BCDToBin(time.Second);
}

/**
 * @brief 配置自动唤醒定时器 (AWT)
 * @param seconds 唤醒的秒数 (1 ~ 65535)
 */
void RTC_SetAwakeTimer(uint16_t seconds)
{
    RTC_AWTTypeDef awt;
    
    // 关闭 AWT，防止配置期间触发
    RTC_AWTCmd(DISABLE);
    
    // 清空标志位
    RTC_ClearITPendingBit(RTC_IT_AWTIMER);
    
    // 配置 AWT 参数：用 1Hz 时钟源（1秒走1步）
    awt.AWT_ClockSource = RTC_AWTSOURCE_FROM_RTC1HZ_1;
    awt.AWT_ARRValue = seconds; 
    RTC_AWTConfig(&awt);
    
    // 开启 AWT 中断
    RTC_ITConfig(RTC_IT_AWTIMER, ENABLE);
    
    // 启动 AWT
    RTC_AWTCmd(ENABLE);
}

/**
 * @brief 休眠前配置：关 1 秒中断，保留 AWT 中断用于唤醒
 */
void RTC_SleepEnable(void)
{
    // 关掉 1 秒周期中断（省电）
    RTC_ITConfig(RTC_IT_INTERVAL, DISABLE);
    // 注意：不要关 NVIC_DisableIRQ(RTC_IRQn)，也不要关 AWT 中断
}

/**
 * @brief 唤醒后恢复：关 AWT，开 1 秒中断
 */
void RTC_WakeupRestore(void)
{
    // 关闭 AWT
    RTC_AWTCmd(DISABLE);
    RTC_ITConfig(RTC_IT_AWTIMER, DISABLE);
    
    // 恢复 1 秒周期中断
    RTC_ITConfig(RTC_IT_INTERVAL, ENABLE);
}