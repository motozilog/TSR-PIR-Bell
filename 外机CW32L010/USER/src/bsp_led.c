/**
 * @file bsp_led.c
 * @brief LED PB03 初始化
 */
#include "bsp_led.h"

void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.Pins = LED_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_Init(LED_PORT, &GPIO_InitStructure);
    GPIO_WritePin(LED_PORT, LED_PIN, GPIO_Pin_RESET);
}