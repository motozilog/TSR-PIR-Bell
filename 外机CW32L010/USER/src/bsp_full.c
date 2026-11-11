/**
 * @file bsp_full.c
 * @brief TP4057 充满指示检测
 *        低电平 = 充满（外部已上拉）
 *        FULL_Scan 仅在"高→低"跳变时返回 1，只报一次
 *        上电 10s 内不检查，仅跟踪电平
 */
#include "bsp_full.h"
#include "bsp_systick.h"

#define FULL_STARTUP_IGNORE_MS   10000   /* 上电 10s 内不检查 */

static uint8_t  s_last = 0;
static uint32_t s_boot_tick = 0;

void FULL_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.Pins = FULL_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT;   /* 外部已上拉 */
    GPIO_Init(FULL_PORT, &GPIO_InitStructure);

    s_last = FULL_IsCharged();
    s_boot_tick = GetTick();
}

uint8_t FULL_IsCharged(void)
{
    return (GPIO_ReadPin(FULL_PORT, FULL_PIN) == GPIO_Pin_RESET) ? 1 : 0;
}

uint8_t FULL_Scan(void)
{
    uint8_t cur = FULL_IsCharged();

    /* 上电 10s 内不检查，仅跟踪电平 */
    if ((GetTick() - s_boot_tick) < FULL_STARTUP_IGNORE_MS)
    {
        s_last = cur;
        return 0;
    }

    uint8_t evt = 0;

    if (cur && !s_last)
    {
        evt = 1;   /* 高→低（未充满→充满）跳变，新事件 */
    }
    s_last = cur;
    return evt;
}

/**
 * @brief 配置 PB02 (FULL) 为下降沿中断唤醒
 */
void FULL_GpioSleepEnable(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.Pins = FULL_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT;   /* 外部已上拉 */
    GPIO_InitStructure.IT   = GPIO_IT_FALLING;   /* 充满时高->低跳变 */
    GPIO_Init(FULL_PORT, &GPIO_InitStructure);

    /* 清挂起标志 */
    CW_GPIOB->ICR_f.PIN2 = 0;

    /* 使能 NVIC */
    NVIC_EnableIRQ(GPIOB_IRQn);
}