/**
 * @file bsp_ws2812.h
 * @brief WS2812 RGB LED 驱动（IO21，单颗）
 */
#ifndef __BSP_WS2812_H__
#define __BSP_WS2812_H__

#include "esp_err.h"
#include <stdint.h>

/* WS2812 数据引脚 */
#define WS2812_GPIO_NUM     21

/* LED 数量 */
#define WS2812_LED_COUNT    1

/* 默认亮度百分比 10~100 */
#define WS2812_BRIGHTNESS_DEFAULT   50

/**
 * @brief 初始化 WS2812（RMT 后端）
 * @return ESP_OK=成功
 */
esp_err_t WS2812_Init(void);

/**
 * @brief 设置颜色（内部按当前亮度缩放）
 * @param r 红 0~255
 * @param g 绿 0~255
 * @param b 蓝 0~255
 */
esp_err_t WS2812_SetColor(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 设置亮度百分比，范围 10~100，超范围会被 clamp
 * @param percent 亮度百分比
 */
esp_err_t WS2812_SetBrightness(uint8_t percent);

/**
 * @brief 熄灭
 */
esp_err_t WS2812_Off(void);

#endif /* __BSP_WS2812_H__ */