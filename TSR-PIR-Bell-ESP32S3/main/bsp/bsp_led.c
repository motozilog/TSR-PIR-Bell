#include "bsp_led.h"

//配置输出寄存器
#define GPIO_OUTPUT_PIN_SEL  (1ULL<<LED_PIN)

/**
 * @函数说明        LED的初始化
 *                  在启动最早期调用，确保 LED 熄灭（输出低电平）
 */
void LedGpioConfig(void)
{
    gpio_config_t gpio_init_struct = {0};

    //配置IO为通用IO
    esp_rom_gpio_pad_select_gpio(LED_PIN);

    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;             //不使用中断
    gpio_init_struct.mode = GPIO_MODE_OUTPUT;                   //输出模式
    gpio_init_struct.pull_up_en = GPIO_PULLUP_DISABLE;          //失能上拉（防止漏电微亮）
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_ENABLE;       //使能下拉（确保默认低电平）
    gpio_init_struct.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;        //使用输出寄存器

    //将以上参数配置到引脚
    gpio_config( &gpio_init_struct );

    // 设置引脚输出低电平，立刻让 LED 熄灭
    gpio_set_level(LED_PIN, 0);
}

/**
 * @函数说明        设置LED亮（高电平亮）
 */
void LedOn(void)
{
    gpio_set_level(LED_PIN, 1);
}

/**
 * @函数说明        设置LED灭（低电平灭）
 */
void LedOff(void)
{
    gpio_set_level(LED_PIN, 0);
}