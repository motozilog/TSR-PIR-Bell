/**
 * @file bsp_key.c
 * @brief 按键 PA01（功能键）+ PA00（复位配置键）消抖状态机
 */
#include "bsp_key.h"
#include "bsp_systick.h"

/******************************************************************************
 * 功能键 PA01
 ******************************************************************************/
typedef enum {
    KEY_IDLE = 0,
    KEY_DEBOUNCE_PRESS,
    KEY_PRESSED,
    KEY_DEBOUNCE_RELEASE
} KeyState_t;

static KeyState_t s_state = KEY_IDLE;
static uint32_t   s_tick  = 0;

/**
 * @brief 功能键 PA01 初始化（轮询模式，同时清掉中断使能）
 *        唤醒后调用，保证 PA01 回轮询、不残留中断
 */
void Key_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.Pins = KEY_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT_PULLUP;
    GPIO_InitStructure.IT   = GPIO_IT_NONE;   /* 关中断使能 */
    GPIO_Init(KEY_PORT, &GPIO_InitStructure);

    /* 清一次挂起标志，防止残留 */
    CW_GPIOA->ICR_f.PIN1 = 0;

    /* 状态机复位 */
    s_state = KEY_IDLE;
}

/**
 * @brief 配置 PA01 为下降沿中断，用于 DeepSleep 唤醒
 *        照官方 demo：一次 GPIO_Init 同时给 Mode 和 IT
 */
void Key_GpioSleepEnable(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.Pins = KEY_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT_PULLUP;
    GPIO_InitStructure.IT   = GPIO_IT_FALLING;
    GPIO_Init(KEY_PORT, &GPIO_InitStructure);

    /* 按位清 PA01 挂起标志（不用整寄存器写） */
    CW_GPIOA->ICR_f.PIN1 = 0;

    /* 使能 NVIC */
    NVIC_EnableIRQ(GPIOA_IRQn);
}

KeyEvent_t Key_Scan(void)
{
    uint8_t raw = (GPIO_ReadPin(KEY_PORT, KEY_PIN) == GPIO_Pin_RESET) ? 1 : 0;

    switch (s_state)
    {
        case KEY_IDLE:
            if (raw) { s_state = KEY_DEBOUNCE_PRESS; s_tick = GetTick(); }
            break;

        case KEY_DEBOUNCE_PRESS:
            if ((GetTick() - s_tick) >= KEY_DEBOUNCE_MS)
            {
                if (raw) { s_state = KEY_PRESSED; return KEY_EVENT_PRESS; }
                s_state = KEY_IDLE;
            }
            break;

        case KEY_PRESSED:
            if (!raw) { s_state = KEY_DEBOUNCE_RELEASE; s_tick = GetTick(); }
            break;

        case KEY_DEBOUNCE_RELEASE:
            if ((GetTick() - s_tick) >= KEY_DEBOUNCE_MS)
            {
                if (raw) { s_state = KEY_PRESSED; }
                else     { s_state = KEY_IDLE; return KEY_EVENT_RELEASE; }
            }
            break;

        default:
            s_state = KEY_IDLE;
            break;
    }

    return KEY_EVENT_NONE;
}

/******************************************************************************
 * 复位配置键 PA00
 ******************************************************************************/
typedef enum {
    CFG_IDLE = 0,
    CFG_DEBOUNCE_PRESS,
    CFG_PRESSED,
    CFG_DEBOUNCE_RELEASE
} CfgKeyState_t;

static CfgKeyState_t s_cfg_state = CFG_IDLE;
static uint32_t      s_cfg_tick  = 0;

void CfgKey_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.Pins = CFG_KEY_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT_PULLUP;
    GPIO_Init(CFG_KEY_PORT, &GPIO_InitStructure);
}

uint8_t CfgKey_Scan(void)
{
    uint8_t raw = (GPIO_ReadPin(CFG_KEY_PORT, CFG_KEY_PIN) == GPIO_Pin_RESET) ? 1 : 0;

    switch (s_cfg_state)
    {
        case CFG_IDLE:
            if (raw) { s_cfg_state = CFG_DEBOUNCE_PRESS; s_cfg_tick = GetTick(); }
            break;

        case CFG_DEBOUNCE_PRESS:
            if ((GetTick() - s_cfg_tick) >= CFG_KEY_DEBOUNCE_MS)
            {
                if (raw) { s_cfg_state = CFG_PRESSED; return 1; }
                s_cfg_state = CFG_IDLE;
            }
            break;

        case CFG_PRESSED:
            if (!raw) { s_cfg_state = CFG_DEBOUNCE_RELEASE; s_cfg_tick = GetTick(); }
            break;

        case CFG_DEBOUNCE_RELEASE:
            if ((GetTick() - s_cfg_tick) >= CFG_KEY_DEBOUNCE_MS)
            {
                if (raw) { s_cfg_state = CFG_PRESSED; }
                else     { s_cfg_state = CFG_IDLE; }
            }
            break;

        default:
            s_cfg_state = CFG_IDLE;
            break;
    }

    return 0;
}