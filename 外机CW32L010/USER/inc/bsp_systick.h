/**
 * @file bsp_systick.h
 * @brief SysTick 1ms 时基
 */
#ifndef __BSP_SYSTICK_H__
#define __BSP_SYSTICK_H__

#include "main.h"

void     SysTick_Init(void);
uint32_t GetTick(void);
void     Delay_ms(uint32_t ms);

#endif /* __BSP_SYSTICK_H__ */