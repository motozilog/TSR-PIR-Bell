/**
 * @file bsp_systick.c
 * @brief SysTick 1ms 时基实现
 */
#include "bsp_systick.h"

static volatile uint32_t s_tick_ms = 0;

void SysTick_Init(void)
{
    SysTick->LOAD = (SystemCoreClock / 1000U) - 1U;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk   |
                    SysTick_CTRL_ENABLE_Msk;
    NVIC_SetPriority(SysTick_IRQn, 0);
}

void SysTick_Handler(void)
{
    s_tick_ms++;
}

uint32_t GetTick(void) { return s_tick_ms; }

void Delay_ms(uint32_t ms)
{
    uint32_t start = s_tick_ms;
    while ((s_tick_ms - start) < ms);
}