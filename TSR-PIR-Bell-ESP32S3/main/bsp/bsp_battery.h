/**
 * @file bsp_battery.h
 * @brief 电池电压采样（ESP32S3 GPIO3，100K/100K 分压）
 *
 * 接法：电池正极 → 100K → GPIO3 → 100K → GND
 *   Vgpio = Vbatt × 100 / (100 + 100) = Vbatt × 100 / 200
 *   Vbatt = Vgpio × 200 / 100
 */
#ifndef _BSP_BATTERY_H_
#define _BSP_BATTERY_H_

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 启动后延时多久开始采样（秒），0=立即开始 */
#define BATT_START_DELAY_SEC        0

/* 采样周期（秒） */
#define BATT_SAMPLE_INTERVAL_SEC    5

/* 采样次数（与 CW32 一致） */
#define BATT_SAMPLE_COUNT           10

/* 每次采样间隔（ms） */
#define BATT_SAMPLE_INTERVAL_MS     10

/* 任务参数 */
#define BATT_TASK_STACK_SIZE        4096
#define BATT_TASK_PRIORITY          3

/**
 * @brief 初始化电池采样模块：配置 ADC 并创建采样任务
 *        任务启动后先延时 BATT_START_DELAY_SEC 秒，再每
 *        BATT_SAMPLE_INTERVAL_SEC 秒采样一次并打印日志
 */
esp_err_t BatteryInit(void);

/**
 * @brief 采样 10 次，冒泡排序，去掉最小 2、最大 2，取中间 6 个平均
 * @return 平均后的 raw（0~4095）
 */
uint16_t BatteryMeasure10TimesAvg(void);

/**
 * @brief 由 raw 换算电池电压（mV）
 *        mv = raw × 3300 × 200 / (4095 × 100)
 */
uint16_t BatteryRawToBattMv(uint16_t raw);

/**
 * @brief 由 raw 换算 GPIO 处电压（mV）
 *        mv = raw × 3300 / 4095
 */
uint16_t BatteryRawToGpioMv(uint16_t raw);

#ifdef __cplusplus
}
#endif

#endif /* _BSP_BATTERY_H_ */