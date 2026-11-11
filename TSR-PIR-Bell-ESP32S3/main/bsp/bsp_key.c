#include "bsp_key.h"

//配置引脚寄存器
#define GPIO_INPUT_PIN_SEL  (1ULL<<KEY_PIN)

/**
 * @函数说明        按键引脚初始化
 * @传入参数        无
 * @函数返回        无
 */
void KeyGpioConfig(void)
{
    //初始化GPIO配置结构 为 零
    gpio_config_t io_conf = {};
    //失能中断
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //设置输入引脚
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    //设置输入模式
    io_conf.mode = GPIO_MODE_INPUT;
    //使能上拉电阻
    io_conf.pull_up_en = 1;
    //失能下拉电阻
    io_conf.pull_down_en = 0;
    ////用给定的设置配置GPIO
    gpio_config(&io_conf);
}

/**
 * @函数说明        获取按键引脚上的电平状态
 * @传入参数        无
 * @函数返回        0=按键被按下    1=按键没有被按下
 */
bool GetKeyValue(void)
{
    //如果按键状态为0
    if( gpio_get_level(KEY_PIN) == 0 )
    {
        //延时消抖，使用该延时需要加入对应头文件
        vTaskDelay(100 / portTICK_PERIOD_MS);
        // 如果按键状态还是0，说明按键真的按下
        if( gpio_get_level(KEY_PIN) == 0 )
        {
            return 0;
        }
    }
    return 1;
}