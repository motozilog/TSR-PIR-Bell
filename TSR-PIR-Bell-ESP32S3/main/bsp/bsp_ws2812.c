/**
 * @file bsp_ws2812.c
 * @brief WS2812 RGB LED 驱动（IO21，单颗，RMT 后端）
 */
#include "bsp_ws2812.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "BSP_WS2812";

static led_strip_handle_t s_strip = NULL;

/* 当前亮度百分比，默认 50 */
static uint8_t s_brightness = WS2812_BRIGHTNESS_DEFAULT;

esp_err_t WS2812_Init(void)
{
    /* LED strip 配置 */
    led_strip_config_t strip_config = {
        .strip_gpio_num = WS2812_GPIO_NUM,
        .max_leds = WS2812_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,   /* WS2812 是 GRB 顺序 */
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    /* RMT 后端配置 */
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,   /* 10MHz，1 tick = 0.1us */
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 初始熄灭 */
    led_strip_clear(s_strip);

    ESP_LOGI(TAG, "WS2812 init done (GPIO=%d, count=%d, brightness=%u%%)",
             WS2812_GPIO_NUM, WS2812_LED_COUNT, (unsigned)s_brightness);
    return ESP_OK;
}

esp_err_t WS2812_SetColor(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_strip == NULL) return ESP_ERR_INVALID_STATE;

    /* 按当前亮度百分比缩放，整数运算避免浮点 */
    uint8_t r_out = (uint8_t)(((uint32_t)r * s_brightness) / 100);
    uint8_t g_out = (uint8_t)(((uint32_t)g * s_brightness) / 100);
    uint8_t b_out = (uint8_t)(((uint32_t)b * s_brightness) / 100);

    esp_err_t ret = led_strip_set_pixel(s_strip, 0, r_out, g_out, b_out);
    if (ret != ESP_OK) return ret;

    return led_strip_refresh(s_strip);
}

esp_err_t WS2812_SetBrightness(uint8_t percent)
{
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;

    s_brightness = percent;
    ESP_LOGI(TAG, "brightness set to %u%%", (unsigned)s_brightness);
    return ESP_OK;
}

esp_err_t WS2812_Off(void)
{
    if (s_strip == NULL) return ESP_ERR_INVALID_STATE;
    return led_strip_clear(s_strip);
}