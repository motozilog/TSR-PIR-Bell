#include "bsp_led.h"

//配置输出寄存器
#define GPIO_OUTPUT_PIN_SEL  (1ULL<<LED_PIN)

/**
 * @函数说明        LED的初始化
 *
 */
void LedGpioConfig(void)
{
    gpio_config_t gpio_init_struct = {0};

    //配置IO为通用IO
    esp_rom_gpio_pad_select_gpio(LED_PIN);

    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;             //不使用中断
    gpio_init_struct.mode = GPIO_MODE_OUTPUT;                   //输出模式
    gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;           //使能上拉模式
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;      //失能下拉模式
    gpio_init_struct.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;        //使用GPIO9输出寄存器

    //将以上参数配置到引脚
    gpio_config( &gpio_init_struct );

    //设置引脚输出高电平，默认不让LED亮
    gpio_set_level(LED_PIN, 1);
}

/**
 * @函数说明        设置LED亮
 *
 */
void LedOn(void)
{
    gpio_set_level(LED_PIN, 0);
}

/**
 * @函数说明        设置LED灭
 *
 */
void LedOff(void)
{
    gpio_set_level(LED_PIN, 1);
}