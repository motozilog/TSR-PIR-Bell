#ifndef _BSP_ES8311_H_
#define _BSP_ES8311_H_

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 固定音频参数：44.1kHz / 16bit / 2ch */
#define ES8311_SAMPLE_RATE        44100
#define ES8311_DEFAULT_BITS       16
#define ES8311_DEFAULT_CHANNELS   2

#define ES8311_MCLK_MULTIPLE      384
#define ES8311_VOICE_VOLUME       70

#define ES8311_I2C_SDA          GPIO_NUM_5
#define ES8311_I2C_SCL          GPIO_NUM_4
#define ES8311_I2C_NUM          I2C_NUM_0
#define ES8311_I2C_FREQ_HZ      100000

#define ES8311_I2S_NUM          I2S_NUM_0
#define ES8311_I2S_MCK_IO       GPIO_NUM_6
#define ES8311_I2S_BCK_IO       GPIO_NUM_14
#define ES8311_I2S_WS_IO        GPIO_NUM_12
#define ES8311_I2S_DO_IO        GPIO_NUM_11
#define ES8311_I2S_DI_IO        I2S_GPIO_UNUSED
#define ES8311_PA_CTRL_IO       GPIO_NUM_10

esp_err_t Es8311AudioInit(void);
esp_err_t Es8311PlayPcm(const uint8_t *data, size_t len);
esp_err_t Es8311PaOn(void);
esp_err_t Es8311PaOff(void);
bool Es8311IsReady(void);
esp_err_t Es8311SetVolume(uint8_t vol);
uint32_t Es8311GetSampleRate(void);
uint8_t  Es8311GetChannels(void);
uint8_t  Es8311GetBits(void);
/* 按实际音频参数重配 I2S 时钟并打开 codec
 * rate: 8000~96000, channels: 1/2, bits: 仅支持 16
 * 不合法返回 ESP_ERR_INVALID_ARG
 */
esp_err_t Es8311PlayConfigure(uint32_t rate, uint8_t channels, uint8_t bits);
#ifdef __cplusplus
}
#endif

#endif