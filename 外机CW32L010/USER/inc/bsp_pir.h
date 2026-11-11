/**
 * @file bsp_pir.h
 * @brief PIR 人体感应 PA02（高电平触发）
 */
#ifndef __BSP_PIR_H__
#define __BSP_PIR_H__

#include "main.h"

#define  PIR_PORT                      CW_GPIOA
#define  PIR_PIN                       GPIO_PIN_2
#define  PIR_DEBOUNCE_MS               50

void PIR_Init(void);
void PIR_GpioSleepEnable(void); /* 新增：休眠前配置上升沿唤醒 */
uint8_t PIR_IsActive(void);
uint8_t PIR_Scan(void);

#endif /* __BSP_PIR_H__ */