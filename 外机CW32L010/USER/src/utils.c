/**
 * @file utils.c
 * @brief 公共工具函数：Unix timestamp 换算（基准 2020-01-01 00:00:00）
 */
#include "utils.h"
#include "app_config.h"  // 引入 APP_DEV_MODE
#include <string.h>
#include "bsp_rtc.h"

/* 平年每月天数 */
static const uint8_t s_days_in_month[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/* 判断闰年（2020~2099 范围，能被 4 整除即闰年） */
static uint8_t IsLeapYear(uint16_t year)
{
    return ((year % 4) == 0) ? 1 : 0;
}

/* 某年某月的天数 */
static uint8_t DaysInMonth(uint16_t year, uint8_t month)
{
    if (month == 2 && IsLeapYear(year))
        return 29;
    return s_days_in_month[month - 1];
}

uint32_t Utils_DateTimeToUnix(uint16_t year, uint8_t month, uint8_t day,
                              uint8_t hour, uint8_t minute, uint8_t second)
{
    uint32_t days = 0;
    uint16_t y;
    uint8_t  m;

    /* 累加 2020 到 year-1 的整年天数 */
    for (y = 2020; y < year; y++)
    {
        days += IsLeapYear(y) ? 366 : 365;
    }

    /* 累加 year 年 1 到 month-1 的整月天数 */
    for (m = 1; m < month; m++)
    {
        days += DaysInMonth(year, m);
    }

    /* 加上本月已过的天数（day - 1） */
    days += (day - 1);

    /* 总秒数 */
    return days * 86400UL
         + (uint32_t)hour * 3600UL
         + (uint32_t)minute * 60UL
         + (uint32_t)second;
}

void Utils_UnixToDateTime(uint32_t ts,
                          uint16_t *year, uint8_t *month, uint8_t *day,
                          uint8_t *hour, uint8_t *minute, uint8_t *second)
{
    uint32_t days;
    uint16_t y = 2020;
    uint8_t  m = 1;

    /* 拆出天数和日内秒数 */
    days = ts / 86400UL;
    ts   = ts % 86400UL;

    /* 算年月日 */
    while (1)
    {
        uint16_t days_in_year = IsLeapYear(y) ? 366 : 365;
        if (days < days_in_year) break;
        days -= days_in_year;
        y++;
    }

    while (1)
    {
        uint8_t dim = DaysInMonth(y, m);
        if (days < dim) break;
        days -= dim;
        m++;
    }

    *year   = y;
    *month  = m;
    *day    = (uint8_t)(days + 1);
    *hour   = (uint8_t)(ts / 3600UL);
    *minute = (uint8_t)((ts % 3600UL) / 60UL);
    *second = (uint8_t)(ts % 60UL);
}


uint32_t Utils_GetCompileTimeUnix(void)
{
    const char *date = __DATE__;
    const char *time = __TIME__;
    
    const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon_str[4] = {date[0], date[1], date[2], '\0'};
    uint8_t month = (strstr(months, mon_str) - months) / 3 + 1;
    
    uint8_t day = (date[4] == ' ' ? (date[5] - '0') : ((date[4] - '0') * 10 + (date[5] - '0')));
    uint16_t year = (date[7] - '0') * 1000 + (date[8] - '0') * 100 + (date[9] - '0') * 10 + (date[10] - '0');
    
    uint8_t hour = (time[0] - '0') * 10 + (time[1] - '0');
    uint8_t minute = (time[3] - '0') * 10 + (time[4] - '0');
    uint8_t second = (time[6] - '0') * 10 + (time[7] - '0');
    
    return Utils_DateTimeToUnix(year, month, day, hour, minute, second);
}

// 计算距离下次定时唤醒的秒数
uint16_t Utils_GetNextWakeupInterval(void)
{
    uint32_t now_ts = RTC_GetUnixTime();
    uint32_t compile_ts = Utils_GetCompileTimeUnix();
    uint32_t wait_sec = 60; // 默认安全值

    // 1. 如果没有校时（当前时间 < 编译时间），统一 60 秒唤醒
    if (now_ts < compile_ts) {
        return 60;
    }

#if APP_DEV_MODE
    // 2. 开发模式：每 30 秒（第 1 秒 和 第 31 秒）
    uint8_t sec = now_ts % 60;
    if (sec < 1) {
        wait_sec = 1 - sec;
    } else if (sec < 31) {
        wait_sec = 31 - sec;
    } else {
        wait_sec = 60 - sec + 1; 
    }

    if (wait_sec == 0) wait_sec = UTILS_DEV_INTERVAL_SEC;
    
    // 开发模式上限
    if (wait_sec > UTILS_DEV_MAX_SLEEP_SEC) {
        wait_sec = UTILS_DEV_MAX_SLEEP_SEC;
    }

#else
    // 3. 生产模式：每 15 分钟的 01 秒
    uint32_t sec_of_day = now_ts % 86400;
    uint32_t min_of_day = sec_of_day / 60;
    uint32_t sec_of_min = sec_of_day % 60;
    
    uint32_t next_min = (min_of_day / UTILS_PROD_INTERVAL_MIN + 1) * UTILS_PROD_INTERVAL_MIN;
    
    if (min_of_day % UTILS_PROD_INTERVAL_MIN == 0 && sec_of_min < 1) {
        next_min = min_of_day;
    }
    
    uint32_t next_sec_of_day = next_min * 60 + 1;
    
    if (next_sec_of_day <= sec_of_day) {
        next_sec_of_day += 86400; 
    }
    
    wait_sec = next_sec_of_day - sec_of_day;
    
    // 生产模式上限
    if (wait_sec > UTILS_PROD_MAX_SLEEP_SEC) {
        wait_sec = UTILS_PROD_MAX_SLEEP_SEC;
    }
#endif

    if (wait_sec == 0) wait_sec = 1;

    return (uint16_t)wait_sec;
}