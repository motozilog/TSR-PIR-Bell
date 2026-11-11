#ifndef _BSP_KEY_H_
#define _BSP_KEY_H_

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//设置按键的引脚
#define KEY_PIN     0

/**
 * @函数说明        按键引脚初始化
 * @传入参数
 * @函数返回
 */
void KeyGpioConfig(void);

/**
 * @函数说明        获取按键引脚上的电平状态
 * @传入参数        无
 * @函数返回        0=按键被按下    1=按键没有被按下
 */
bool GetKeyValue(void);

#endif