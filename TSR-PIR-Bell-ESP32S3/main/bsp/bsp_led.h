#ifndef _BSP_LED_H_
#define _BSP_LED_H_

#include "driver/gpio.h"

//设置LED引脚
#define  LED_PIN  48

/**
 * @brief   LED初始化
 *
 */
void LedGpioConfig(void);

/**
 * @brief       设置LED亮
 *
 */
void LedOn(void);

/**
 * @brief       设置LED灭
 *
 */
void LedOff(void);
#endif