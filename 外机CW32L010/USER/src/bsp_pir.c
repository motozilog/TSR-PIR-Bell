/**
 * @file bsp_pir.c
 * @brief PIR 人体感应检测
 */
#include "bsp_pir.h"
#include "bsp_systick.h"

static uint8_t  s_last = 0;

void PIR_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.Pins = PIR_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT;
    GPIO_Init(PIR_PORT, &GPIO_InitStructure);

    s_last = (GPIO_ReadPin(PIR_PORT, PIR_PIN) == GPIO_Pin_SET) ? 1 : 0;
}

uint8_t PIR_IsActive(void)
{
    return (GPIO_ReadPin(PIR_PORT, PIR_PIN) == GPIO_Pin_SET) ? 1 : 0;
}

uint8_t PIR_Scan(void)
{
    uint8_t cur = PIR_IsActive();
    uint8_t evt = 0;

    if (cur && !s_last)
    {
        evt = 1;   /* 上升沿，新触发 */
    }
    s_last = cur;
    return evt;
}

/**
 * @brief 配置 PA02（PIR）为上升沿中断唤醒
 */
void PIR_GpioSleepEnable(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.Pins = PIR_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT; // PIR 模块通常推挽输出，不需要内部上拉，或按硬件决定
    GPIO_InitStructure.IT   = GPIO_IT_RISING;  // 上升沿触发（人来了输出高电平）
    GPIO_Init(PIR_PORT, &GPIO_InitStructure);

    /* 清除 PA02 挂起标志 */
    CW_GPIOA->ICR_f.PIN2 = 0;

    /* 使能 GPIOA NVIC */
    NVIC_EnableIRQ(GPIOA_IRQn);
}