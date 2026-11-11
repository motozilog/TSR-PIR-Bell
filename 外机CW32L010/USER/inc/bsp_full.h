/**
 * @file bsp_full.h
 * @brief TP4057 充电满指示 PB02（STDBY，低电平 = 充满）
 */
#ifndef __BSP_FULL_H__
#define __BSP_FULL_H__

#include "main.h"

#define  FULL_PORT                     CW_GPIOB
#define  FULL_PIN                      GPIO_PIN_2

void     FULL_Init(void);
void FULL_GpioSleepEnable(void);
uint8_t  FULL_IsCharged(void);   /* 返回 1 = 已充满 */
uint8_t FULL_Scan(void);   /* 返回 1 = 检测到"充满"跳变（高→低），只报一次 */

#endif /* __BSP_FULL_H__ */