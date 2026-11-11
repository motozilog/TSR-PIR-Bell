/**
 * @file bsp_battery.c
 * @brief 电池电压采样（ESP32S3 GPIO3，100K/100K 分压）
 *
 * 算法与 CW32 一致：
 *   采 10 次 → 冒泡排序 → 去掉最小 2、最大 2 → 中间 6 个求平均
 * 换算：
 *   gpio_mv = raw × 3300 / 4095
 *   batt_mv = gpio_mv × 200 / 100
 */
#include "bsp_battery.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP_BATTERY";

/* GPIO3 = ADC1_CH2 */
#define BATT_ADC_GPIO       GPIO_NUM_3
#define BATT_ADC_UNIT       ADC_UNIT_1
#define BATT_ADC_CHANNEL    ADC_CHANNEL_2
#define BATT_ADC_ATTEN      ADC_ATTEN_DB_11
#define BATT_ADC_BITWIDTH   ADC_BITWIDTH_12

/* 参考电压 3300 mV，12 位满量程 4095 */
#define BATT_VREF_MV        3300UL
#define BATT_ADC_FULLSCALE  4095UL

/* 分压比：Vbatt = Vgpio × (100 + 100) / 100 = Vgpio × 200 / 100 */
#define BATT_DIV_NUM        200UL
#define BATT_DIV_DEN        100UL

static adc_oneshot_unit_handle_t s_adc_handle = NULL;

/**
 * @brief 采一次 raw（同步）
 */
static uint16_t BatterySampleOnce(void)
{
    int raw = 0;
    esp_err_t ret = adc_oneshot_read(s_adc_handle, BATT_ADC_CHANNEL, &raw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_read failed: %s", esp_err_to_name(ret));
        return 0;
    }
    if (raw < 0) raw = 0;
    if (raw > (int)BATT_ADC_FULLSCALE) raw = (int)BATT_ADC_FULLSCALE;
    return (uint16_t)raw;
}

uint16_t BatteryMeasure10TimesAvg(void)
{
    uint16_t samples[BATT_SAMPLE_COUNT];
    uint32_t sum = 0;
    uint8_t  i, j;

    for (i = 0; i < BATT_SAMPLE_COUNT; i++) {
        samples[i] = BatterySampleOnce();
        //ESP_LOGI(TAG, "sample[%u] raw=%u", (unsigned)i, (unsigned)samples[i]);
        vTaskDelay(pdMS_TO_TICKS(BATT_SAMPLE_INTERVAL_MS));
    }

    /* 冒泡排序（与 CW32 一致） */
    for (i = 0; i < BATT_SAMPLE_COUNT - 1; i++) {
        for (j = 0; j < BATT_SAMPLE_COUNT - 1 - i; j++) {
            if (samples[j] > samples[j + 1]) {
                uint16_t tmp = samples[j];
                samples[j] = samples[j + 1];
                samples[j + 1] = tmp;
            }
        }
    }

    /* 去掉最小 2、最大 2，取中间 6 个平均 */
    for (i = 2; i < BATT_SAMPLE_COUNT - 2; i++) {
        sum += samples[i];
    }
    return (uint16_t)(sum / (BATT_SAMPLE_COUNT - 4));
}

uint16_t BatteryRawToGpioMv(uint16_t raw)
{
    /* mv = raw × 3300 / 4095 */
    uint32_t mv = ((uint32_t)raw * BATT_VREF_MV) / BATT_ADC_FULLSCALE;
    return (uint16_t)mv;
}

uint16_t BatteryRawToBattMv(uint16_t raw)
{
    /* 先算 GPIO 处电压，再乘分压比，避免 uint32 溢出
     * gpio_mv = raw × 3300 / 4095
     * batt_mv = gpio_mv × 200 / 100
     */
    uint32_t gpio_mv = ((uint32_t)raw * BATT_VREF_MV) / BATT_ADC_FULLSCALE;
    uint32_t batt_mv = (gpio_mv * BATT_DIV_NUM) / BATT_DIV_DEN;
    return (uint16_t)batt_mv;
}

static void BatteryTask(void *args)
{
    (void)args;

    if (BATT_START_DELAY_SEC > 0) {
        ESP_LOGI(TAG, "battery task started, wait %d s before first sample",
                 BATT_START_DELAY_SEC);
        vTaskDelay(pdMS_TO_TICKS(BATT_START_DELAY_SEC * 1000));
    } else {
        ESP_LOGI(TAG, "battery task started, sample immediately");
    }

    while (1) {
        uint16_t raw     = BatteryMeasure10TimesAvg();
        uint16_t gpio_mv = BatteryRawToGpioMv(raw);
        uint16_t batt_mv = BatteryRawToBattMv(raw);

        // ESP_LOGI(TAG, "raw=%u, gpio=%u mV, batt=%u mV (%u.%02u V)",
        //          raw, gpio_mv, batt_mv,
        //          batt_mv / 1000, (batt_mv % 1000) / 10);

        vTaskDelay(pdMS_TO_TICKS(BATT_SAMPLE_INTERVAL_SEC * 1000));
    }
}

esp_err_t BatteryInit(void)
{
    /* 1. ADC oneshot 单元初始化 */
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = BATT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. 反查 GPIO3 对应的 ADC 通道，验证硬件映射 */
    adc_unit_t io_unit;
    adc_channel_t io_chan;
    esp_err_t io_ret = adc_oneshot_io_to_channel(BATT_ADC_GPIO, &io_unit, &io_chan);
    if (io_ret == ESP_OK) {
        ESP_LOGI(TAG, "GPIO%d -> unit=%d, channel=%d",
                 BATT_ADC_GPIO, (int)io_unit, (int)io_chan);
        if (io_chan != BATT_ADC_CHANNEL) {
            ESP_LOGW(TAG, "channel mismatch: io_to_channel=%d, macro=%d",
                     (int)io_chan, (int)BATT_ADC_CHANNEL);
        }
    } else {
        ESP_LOGE(TAG, "GPIO%d is NOT a valid ADC pad: %s",
                 BATT_ADC_GPIO, esp_err_to_name(io_ret));
    }

    /* 3. 通道配置：11dB 衰减，12 位 */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BATT_ADC_ATTEN,
        .bitwidth = BATT_ADC_BITWIDTH,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, BATT_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return ret;
    }

    /* 4. 创建采样任务 */
    BaseType_t ok = xTaskCreate(BatteryTask, "battery_task",
                                BATT_TASK_STACK_SIZE, NULL,
                                BATT_TASK_PRIORITY, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "create battery_task failed");
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "battery init done (GPIO3/ADC1_CH2, 11dB, 12bit, 100K/100K)");
    return ESP_OK;
}