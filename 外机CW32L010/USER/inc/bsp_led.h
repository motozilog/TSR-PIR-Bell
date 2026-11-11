/**
 * @file bsp_led.h
 * @brief LED PB03（推挽输出，低电平点亮/高电平熄灭）
 */
#ifndef __BSP_LED_H__
#define __BSP_LED_H__

#include "main.h"

#define  LED_PORT                      CW_GPIOB
#define  LED_PIN                       GPIO_PIN_3

void LED_Init(void);

#endif /* __BSP_LED_H__ */