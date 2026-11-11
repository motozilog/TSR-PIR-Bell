#include "bsp_es8311.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

static const char *TAG = "BSP_ES8311";

/* ES8311 固定 7 位 I2C 地址 */
#define ES8311_I2C_ADDR_7BIT 0x30

static i2s_chan_handle_t s_tx_handle = NULL;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static const audio_codec_data_if_t *s_i2s_data_if = NULL;
static esp_codec_dev_handle_t s_play_dev = NULL;
static bool s_ready = false;

#define ES8311_POWER_ON_DELAY_MS 500

/* ===== I2C 初始化（新版驱动） ===== */
static esp_err_t bsp_i2c_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = ES8311_I2C_NUM,
        .sda_io_num = ES8311_I2C_SDA,
        .scl_io_num = ES8311_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_LOGI(TAG, "i2c_new_master_bus begin: port=%d SDA=%d SCL=%d pullup=%d",
             ES8311_I2C_NUM, ES8311_I2C_SDA, ES8311_I2C_SCL,
             (int)bus_cfg.flags.enable_internal_pullup);

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);

    ESP_LOGI(TAG, "i2c_new_master_bus end: ret=%s (%d), handle=%p",
             esp_err_to_name(err), (int)err, (void *)s_i2c_bus);

    ESP_RETURN_ON_ERROR(err, TAG, "i2c_new_master_bus failed");

    ESP_LOGI(TAG, "I2C bus created (SDA=%d SCL=%d)", ES8311_I2C_SDA, ES8311_I2C_SCL);
    return ESP_OK;
}

/* ===== I2S 初始化（32bit / STEREO，参照立创 S3） ===== */
static esp_err_t bsp_i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(ES8311_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_handle, NULL),
                        TAG, "i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ES8311_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = ES8311_I2S_MCK_IO,
            .bclk = ES8311_I2S_BCK_IO,
            .ws = ES8311_I2S_WS_IO,
            .dout = ES8311_I2S_DO_IO,
            .din = ES8311_I2S_DI_IO,
            .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_handle, &std_cfg),
                        TAG, "i2s_channel_init_std_mode failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_handle),
                        TAG, "i2s_channel_enable failed");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = ES8311_I2S_NUM,
        .rx_handle = NULL,
        .tx_handle = s_tx_handle,
    };
    s_i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(s_i2s_data_if, ESP_FAIL, TAG, "audio_codec_new_i2s_data failed");

    ESP_LOGI(TAG, "I2S init success (32bit/STEREO default %u Hz)",
             (unsigned)ES8311_SAMPLE_RATE);
    return ESP_OK;
}

/* ===== ES8311 codec 设备初始化（直线流程，失败直接返回） ===== */
static esp_err_t bsp_es8311_codec_init(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c init failed");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if, ESP_FAIL, TAG, "audio_codec_new_gpio failed");

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = ES8311_I2C_NUM,
        .addr = ES8311_I2C_ADDR_7BIT, /* 固定 7 位地址 0x18 */
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(i2c_ctrl_if, ESP_FAIL, TAG, "audio_codec_new_i2c_ctrl failed");

    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = ES8311_PA_CTRL_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };

    const audio_codec_if_t *es8311_dev = NULL;
    int retry_count = 0;

    while (1) {
        es8311_dev = es8311_codec_new(&es8311_cfg);
        if (es8311_dev != NULL) {
            ESP_LOGI(TAG, "es8311_codec_new success after %d retries", retry_count);
            break; // 成功，跳出循环
        }

        retry_count++;
        ESP_LOGW(TAG, "es8311_codec_new retry %d/100", retry_count);
        
        if (retry_count >= 5) {
            ESP_LOGE(TAG, "es8311_codec_new failed, rebooting ESP32...");
            vTaskDelay(pdMS_TO_TICKS(500)); // 给日志打印留点时间
            esp_restart(); // 直接重启
        }

        vTaskDelay(pdMS_TO_TICKS(200)); // 每次失败等待 200ms
    }

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8311_dev,
        .data_if = s_i2s_data_if,
    };
    s_play_dev = esp_codec_dev_new(&codec_dev_cfg);
    ESP_RETURN_ON_FALSE(s_play_dev, ESP_FAIL, TAG, "esp_codec_dev_new failed");

    ESP_LOGI(TAG, "ES8311 codec init success (addr=0x%02X)", ES8311_I2C_ADDR_7BIT);
    return ESP_OK;
}

esp_err_t Es8311AudioInit(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2s_init(), TAG, "i2s init failed");
    ESP_RETURN_ON_ERROR(bsp_es8311_codec_init(), TAG, "es8311 init failed");

    /* ES8311 上电稳定时间 */
    vTaskDelay(pdMS_TO_TICKS(ES8311_POWER_ON_DELAY_MS));

    /* 不再在这里 open，只置 ready 状态 */
    s_ready = true;

    ESP_LOGI(TAG, "Audio init done (%u Hz/%ubit/%uch), waiting for playback to open",
             (unsigned)ES8311_SAMPLE_RATE,
             (unsigned)ES8311_DEFAULT_BITS,
             (unsigned)ES8311_DEFAULT_CHANNELS);
    return ESP_OK;
}

static i2c_master_dev_handle_t s_diag_dev = NULL;

static esp_err_t Es8311DiagDevInit(void)
{
    if (s_diag_dev != NULL) return ESP_OK;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ES8311_I2C_ADDR_7BIT,
        .scl_speed_hz    = ES8311_I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_diag_dev);
}


static esp_err_t Es8311ReadReg(uint8_t reg, uint8_t *val)
{
    if (s_diag_dev == NULL || val == NULL) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_diag_dev,
                                       &reg, 1,
                                       val, 1,
                                       pdMS_TO_TICKS(100));
}

esp_err_t Es8311PlayConfigure(uint32_t rate, uint8_t channels, uint8_t bits)
{
    if (!s_ready || s_play_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (rate < 8000 || rate > 96000 || bits != 16 ||
        (channels != 1 && channels != 2)) {
        ESP_LOGW(TAG, "PlayConfigure invalid: rate=%lu ch=%u bits=%u",
                 (unsigned long)rate, channels, bits);
        return ESP_ERR_INVALID_ARG;
    }

    /* 已打开则先关，避免重复 open；esp_codec_dev 已打开时 open 不会重配采样率 */
    esp_codec_dev_close(s_play_dev);

    /* 确保 I2S 处于 READY 状态：只有 RUNNING 才需要 disable */
    i2s_chan_info_t info;
    if (i2s_channel_get_info(s_tx_handle, &info) == ESP_OK && info.is_enabled) {
        esp_err_t derr = i2s_channel_disable(s_tx_handle);
        if (derr != ESP_OK) {
            ESP_LOGW(TAG, "i2s_channel_disable failed: %s", esp_err_to_name(derr));
        }
    }

    /* 此时 state 应为 READY，reconfig 可安全调用 */
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;

    esp_err_t ret = i2s_channel_reconfig_std_clock(s_tx_handle, &clk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "reconfig_std_clock failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* enable 并检查返回值 */
    ret = i2s_channel_enable(s_tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
        return ret;
    }


        /* ===== 临时诊断：打印重配后的实际时钟 ===== */
    i2s_chan_info_t info2;
    if (i2s_channel_get_info(s_tx_handle, &info2) == ESP_OK) {
        ESP_LOGI(TAG, "after reconfig: sclk=%lu mclk=%lu bclk=%lu",
                 (unsigned long)info2.sclk_hz, (unsigned long)info2.mclk_hz,
                 (unsigned long)info2.bclk_hz);
    }
    /* ===== 诊断结束 ===== */

    /* 用实际参数打开 codec */
    channels = 2;   //必须使用2，打开单声道才正常
    esp_codec_dev_sample_info_t fs = {
        .sample_rate     = rate,
        .channel         = channels,
        .bits_per_sample = bits,
    };

    /* 确保关闭，open 才会重新配置采样率 */
    int close_ret = esp_codec_dev_close(s_play_dev);
    ESP_LOGI(TAG, "pre-open close ret=%d", close_ret);

    ret = esp_codec_dev_open(s_play_dev, &fs);

int val = 0;
if (esp_codec_dev_read_reg(s_play_dev, 0x02, &val) == ESP_CODEC_DEV_OK) {
    ESP_LOGI(TAG, "ES8311 REG02 = 0x%02X", val & 0xFF);
}


    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "codec open OK: %lu Hz / %u ch / %u bit",
                 (unsigned long)rate, channels, bits);
        vTaskDelay(pdMS_TO_TICKS(20));
    } else {
        ESP_LOGE(TAG, "codec open failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t Es8311PaOn(void)
{
    return Es8311PlayConfigure(ES8311_SAMPLE_RATE,
                               ES8311_DEFAULT_CHANNELS,
                               ES8311_DEFAULT_BITS);
}

esp_err_t Es8311PaOff(void)
{
    if (!s_ready || s_play_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_codec_dev_close(s_play_dev);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "PA OFF (close codec OK)");
    } else {
        ESP_LOGE(TAG, "PA OFF failed: %s", esp_err_to_name(ret));
    }
    return ret;
}


uint32_t Es8311GetSampleRate(void)
{
    return ES8311_SAMPLE_RATE;
}

uint8_t Es8311GetChannels(void)
{
    return ES8311_DEFAULT_CHANNELS;
}

uint8_t Es8311GetBits(void)
{
    return ES8311_DEFAULT_BITS;
}

esp_err_t Es8311PlayPcm(const uint8_t *data, size_t len)
{
    ESP_RETURN_ON_FALSE(s_ready && s_play_dev, ESP_ERR_INVALID_STATE,
                        TAG, "audio not initialized");

    esp_err_t ret = esp_codec_dev_write(s_play_dev, (void *)data, len);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_codec_dev_write failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t Es8311SetVolume(uint8_t vol)
{
    if (!s_ready || s_play_dev == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (vol > 100)
        vol = 100;
    esp_err_t ret = esp_codec_dev_set_out_vol(s_play_dev, (int)vol);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "set volume %u failed: %s", vol, esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "volume set to %u", vol);
    }
    return ret;
}

bool Es8311IsReady(void)
{
    return s_ready;
}