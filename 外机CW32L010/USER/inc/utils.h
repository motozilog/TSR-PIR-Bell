#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>

/* Unix timestamp 基准：2020-01-01 00:00:00 = 0 */

/**
 * @brief 年月日时分秒 → 从 2020-01-01 00:00:00 起的秒数
 */
uint32_t Utils_DateTimeToUnix(uint16_t year, uint8_t month, uint8_t day,
                              uint8_t hour, uint8_t minute, uint8_t second);

/**
 * @brief 从 2020-01-01 00:00:00 起的秒数 → 年月日时分秒
 */
void Utils_UnixToDateTime(uint32_t ts,
                          uint16_t *year, uint8_t *month, uint8_t *day,
                          uint8_t *hour, uint8_t *minute, uint8_t *second);

/**
 * @brief 获取编译时的 Unix 时间戳（基准 2020-01-01）
 */
uint32_t Utils_GetCompileTimeUnix(void);

/**
 * @brief 计算距离下次定时唤醒的秒数
 *        未校时返回 60 秒；已校时根据 APP_DEV_MODE 返回对应周期
 */
uint16_t Utils_GetNextWakeupInterval(void);

#endif /* __UTILS_H__ */