/**
 * @file bsp_key.h
 * @brief 按键 PA01（功能键）+ PA00（复位配置键）驱动
 */
#ifndef __BSP_KEY_H__
#define __BSP_KEY_H__

#include "main.h"

/* 消抖时间 */
#define KEY_DEBOUNCE_MS        20
#define CFG_KEY_DEBOUNCE_MS    20

#define KEY_PORT        CW_GPIOA
#define KEY_PIN         GPIO_PIN_1
#define CFG_KEY_PORT    CW_GPIOA
#define CFG_KEY_PIN     GPIO_PIN_0

typedef enum
{
    KEY_EVENT_NONE = 0,
    KEY_EVENT_PRESS,
    KEY_EVENT_RELEASE
} KeyEvent_t;

void Key_Init(void);
void Key_GpioSleepEnable(void);
KeyEvent_t Key_Scan(void);

void CfgKey_Init(void);
uint8_t CfgKey_Scan(void);

#endif /* __BSP_KEY_H__ */