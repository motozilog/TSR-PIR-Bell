#include "bsp_chirp.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include "esp_check.h"
#include "esp_random.h"
#include "app_config.h"
#include "app_chirp.h"
#include "esp_timer.h"

static const char *TAG = "BSP_CHIRP";

/* 状态 */
static uint8_t s_channel = 0;
static bool    s_ready = false;
static uint32_t send_count =0;

/* ===== 切换工作模式（参数顺序：m0, m1） ===== */
bool ChirpSetMode(int m0, int m1)
{
    ESP_LOGI(TAG, "Set mode M0=%d M1=%d, AUX before: %d",
             m0, m1, gpio_get_level(CHIRP_AUX_PIN));

    gpio_set_level(CHIRP_M0_PIN, m0);
    gpio_set_level(CHIRP_M1_PIN, m1);

    int64_t start = xTaskGetTickCount();
    while (gpio_get_level(CHIRP_AUX_PIN) == 1) {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(2000)) {
            ESP_LOGW(TAG, "AUX never went low");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (!ChirpWaitAuxIdle(3000)) {
        ESP_LOGW(TAG, "AUX timeout waiting high");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(5));
    return true;
}

/* ===== 等待 AUX 空闲（高电平）===== */
bool ChirpWaitAuxIdle(uint32_t timeout_ms)
{
    int64_t start = xTaskGetTickCount();
    while (gpio_get_level(CHIRP_AUX_PIN) == 0)
    {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(timeout_ms))
        {
            ESP_LOGW(TAG, "AUX idle timeout");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return true;
}

/* ===== 写寄存器 ===== */
bool ChirpWriteReg(uint8_t reg, uint8_t val)
{
    // 指令：C0 + 起始地址 + 长度 + 参数
    uint8_t cmd[] = { 0xC0, reg, 0x01, val };

    uart_flush_input(CHIRP_UART_NUM);
    uart_write_bytes(CHIRP_UART_NUM, cmd, sizeof(cmd));

    // 响应：C1 + 地址 + 长度 + 参数 = 4 字节
    uint8_t resp[8] = {0};
    int len = uart_read_bytes(CHIRP_UART_NUM, resp, sizeof(resp),
                              pdMS_TO_TICKS(200));

    if (len < 4 || resp[0] != 0xC1 || resp[1] != reg)
    {
        ESP_LOGW(TAG, "Write reg 0x%02X failed (len=%d)", reg, len);
        return false;
    }
    ESP_LOGI(TAG, "Write reg 0x%02X = 0x%02X OK", reg, val);
    return true;
}

/* ===== 读取信道寄存器 ===== */
bool ChirpReadChannelReg(uint8_t *channel)
{
    uint8_t cmd[] = { 0xC1, 0x05, 0x01 };

    uart_flush_input(CHIRP_UART_NUM);
    uart_write_bytes(CHIRP_UART_NUM, cmd, sizeof(cmd));

    uint8_t resp[8] = {0};
    int len = uart_read_bytes(CHIRP_UART_NUM, resp, sizeof(resp),
                              pdMS_TO_TICKS(200));

    if (len < 4)
    {
        ESP_LOGW(TAG, "Read channel: short response (%d bytes)", len);
        return false;
    }

    if (resp[0] != 0xC1 || resp[1] != 0x05 || resp[2] != 0x01)
    {
        ESP_LOGW(TAG, "Read channel: bad response %02X %02X %02X",
                 resp[0], resp[1], resp[2]);
        return false;
    }

    *channel = resp[3];
    return true;
}

/* ===== 发送一帧（变长、指定目标） =====
 * 帧格式：[LEN] [DATA...] [CHECKSUM]
 *
 * 定点模式下 E29 模块会自动在数据前加 [地址高][地址低][信道] 三个字节
 * 作为无线帧头。NetID 是模块级参数，在初始化时写入，不在帧头中体现。
 */
bool ChirpSendFrame(uint16_t addr, uint8_t netid, uint8_t channel,
                    const uint8_t *data, uint8_t len)
{
    if (data == NULL || len == 0 || len > CHIRP_FRAME_MAX_DATA) {
        ESP_LOGE(TAG, "ChirpSendFrame: invalid len=%d", len);
        return false;
    }

    ChirpSetMode(0, 0);                       // 回传输模式


    // 帧缓冲：3 字节定点头 + [LEN + DATA + CHECKSUM]
    uint8_t frame[3 + CHIRP_FRAME_MAX_TOTAL];
    uint8_t idx = 0;

    // 1. 定点传输头（模块会解析并去掉，不上空口）
    frame[idx++] = (addr >> 8) & 0xFF;   // 地址高
    frame[idx++] = (addr >> 0) & 0xFF;   // 地址低
    frame[idx++] = channel;              // 信道

    // 2. 数据帧：[LEN][DATA...][CHECKSUM]
    uint8_t sum = 0;

    frame[idx++] = len;
    sum += len;

    for (uint8_t i = 0; i < len; i++) {
        frame[idx++] = data[i];
        sum += data[i];
    }

    frame[idx++] = sum;

    // 3. 发送
    int written = uart_write_bytes(CHIRP_UART_NUM, (const char *)frame, idx);
    if (written != idx) {
        ESP_LOGE(TAG, "ChirpSendFrame: uart write %d/%d", written, idx);
        return false;
    }

    // 4. 日志：地址 + 内容，一行打印
    printf("I (%lu) %s: TX -> addr=0x%04X netid=0x%02X ch=0x%02X len=%d data:",
           (unsigned long)esp_log_timestamp(), TAG,
           addr, netid, channel, len);
    for (uint8_t i = 0; i < len; i++) {
        printf(" %02X", data[i]);
    }
    printf(" sum:%02X\n", sum);

    printf("\n");

    return true;
}

/* ===== 任务：开机读一次信道（原发送测试代码已注释） ===== */
void ChirpChannelTask(void *args)
{
    (void)args;

    // ===== 第一步：开机获取一次信道 =====
    // ChirpSetMode(0, 1);                       // 进配置模式
    // if (ChirpWaitAuxIdle(500))
    // {
    //     vTaskDelay(pdMS_TO_TICKS(200));
    //     uint8_t ch = 0;
    //     if (ChirpReadChannelReg(&ch))
    //     {
    //         s_channel = ch;
    //         s_ready = true;
    //         ESP_LOGI(TAG, "Channel = 0x%02X (freq = %d MHz)",
    //                  ch, 410 + ch);
    //     }
    // }
    ChirpSetMode(0, 0);                       // 回传输模式
    vTaskDelay(pdMS_TO_TICKS(100));

    // ===== 第二步：发送0xFF的测试代码 =====
    // while (1)
    // {
    //     uint8_t payload[16];
    //     for (int i = 0; i < 16; i++)
    //     {
    //         payload[i] = 0xFF;
    //     }

    //     // 最后 4 位为 ESP32 启动时间（秒），大端序
    //     uint32_t boot_sec = (uint32_t)(esp_timer_get_time() / 1000000LL);
    //     payload[12] = (uint8_t)(boot_sec >> 24);
    //     payload[13] = (uint8_t)(boot_sec >> 16);
    //     payload[14] = (uint8_t)(boot_sec >> 8);
    //     payload[15] = (uint8_t)(boot_sec & 0xFF);

    //     if (send_count < 1) {
    //         ChirpSendFrame(CHIRP_TARGET_ADDR,
    //                        CHIRP_TARGET_NETID,
    //                        CHIRP_TARGET_CHANNEL,
    //                        payload, 16);
    //         send_count++;
    //     }

    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }

    // /* 任务无事可做，挂起自己 */
    vTaskSuspend(NULL);
}

void ChirpRxTask(void *args)
{
    (void)args;

    uint8_t rx_buf[256];
    TickType_t last_beat = xTaskGetTickCount();

    ESP_LOGI(TAG, "RX task started");

    while (1)
    {
        int len = uart_read_bytes(CHIRP_UART_NUM, rx_buf, sizeof(rx_buf),
                                  pdMS_TO_TICKS(50));
        if (len > 0)
        {
            /* 原始 hex 打印，便于排查 */
            printf("I (%lu) %s: RX <- len=%d data:",
                   (unsigned long)esp_log_timestamp(), TAG, len);
            for (int i = 0; i < len; i++) printf(" %02X", rx_buf[i]);
            printf("\n");

            /* 帧校验：[LEN][DATA...][CHECKSUM] */
            if (len < 3) continue;

            uint8_t frame_len = rx_buf[0];              /* LEN */
            if ((uint16_t)len != (uint16_t)(frame_len + 2)) {
                ESP_LOGW(TAG, "frame len mismatch: rx=%d LEN=%u", len, frame_len);
                continue;
            }

            uint8_t sum = 0;
            for (int i = 0; i < (int)(1 + frame_len); i++) sum += rx_buf[i];
            if (sum != rx_buf[1 + frame_len]) {
                ESP_LOGW(TAG, "checksum bad: calc=%02X recv=%02X",
                         sum, rx_buf[1 + frame_len]);
                continue;
            }

            /* DATA = [SRC_H][SRC_L][OPCODE][PAYLOAD...] */
            AppChirp_OnFrame(&rx_buf[1], frame_len);
        }


        // if ((xTaskGetTickCount() - last_beat) > pdMS_TO_TICKS(1000)) {
        //     last_beat = xTaskGetTickCount();
        //     ESP_LOGI(TAG, "RX task alive (AUX=%d, M0=%d, M1=%d)",
        //              gpio_get_level(CHIRP_AUX_PIN),
        //              gpio_get_level(CHIRP_M0_PIN),
        //              gpio_get_level(CHIRP_M1_PIN));
        // }
    }
}


/* ===== 临时测试：发送 AT 指令并打印结果 ===== */
static void ChirpTestAT(const char *at_cmd)
{
    ESP_LOGI(TAG, "Entering config mode for AT Command: %s", at_cmd);
    
    // E29 手册要求：M1=1, M0=0 为配置模式
    gpio_set_level(CHIRP_M0_PIN, 0);
    gpio_set_level(CHIRP_M1_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100)); // 等待模式切换

    // 发送指令
    uart_flush_input(CHIRP_UART_NUM);
    uart_write_bytes(CHIRP_UART_NUM, at_cmd, strlen(at_cmd));

    // 读取返回
    uint8_t at_rx_buf[512] = {0};
    int at_len = uart_read_bytes(CHIRP_UART_NUM, at_rx_buf, sizeof(at_rx_buf) - 1, pdMS_TO_TICKS(1000));
    
    if (at_len > 0) {
        printf("\r\n--- AT Response ---\r\n");
        printf("%s", (char *)at_rx_buf);
        printf("\r\n------------------------\r\n");
    } else {
        ESP_LOGW(TAG, "AT timeout or no response");
    }

    // 恢复传输模式（M0=0, M1=0）
    gpio_set_level(CHIRP_M0_PIN, 0);
    gpio_set_level(CHIRP_M1_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
}

/* ===== 对外接口 ===== */

esp_err_t ChirpInit(void)
{
    ESP_LOGI(TAG, "Initializing Chirp module");

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << CHIRP_M0_PIN) | (1ULL << CHIRP_M1_PIN),
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "gpio_config failed");

    gpio_config_t aux_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << CHIRP_AUX_PIN),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&aux_conf), TAG, "aux gpio_config failed");

    // 默认进入传输模式（M0=0, M1=0）
    ChirpSetMode(0, 0);

    uart_config_t uart_cfg = {
        .baud_rate = CHIRP_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_param_config(CHIRP_UART_NUM, &uart_cfg),
                        TAG, "uart_param_config failed");

    ESP_RETURN_ON_ERROR(uart_set_pin(CHIRP_UART_NUM,
                                     CHIRP_UART_TX_PIN,
                                     CHIRP_UART_RX_PIN,
                                     UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG, "uart_set_pin failed");

    ESP_RETURN_ON_ERROR(uart_driver_install(CHIRP_UART_NUM, 512, 0, 0, NULL, 0),
                        TAG, "uart_driver_install failed");

    ESP_LOGI(TAG, "Chirp init done (TX=IO%d, RX=IO%d, M0=IO%d, M1=IO%d, AUX=IO%d)",
             CHIRP_UART_TX_PIN, CHIRP_UART_RX_PIN,
             CHIRP_M0_PIN, CHIRP_M1_PIN, CHIRP_AUX_PIN);
    



    ChirpTestAT("AT+HELP=?");
    ChirpTestAT("AT+DEFAULT");
    ChirpTestAT("AT+RESET");



    // === 初始化配置：写 NetID + 定点传输模式 (+ 开发模式设功率) ===
    ESP_LOGI(TAG, "Configuring Chirp module...");
    ChirpSetMode(0, 1);
    if (ChirpWaitAuxIdle(500))
    {
        vTaskDelay(pdMS_TO_TICKS(200));

        ChirpWriteReg(0x02, CHIRP_TARGET_NETID);   // NETID = 0x00

        if (ChirpEnableFixedPoint())
            ESP_LOGI(TAG, "Fixed-point mode enabled");
        else
            ESP_LOGW(TAG, "Enable fixed-point failed");

#if APP_DEV_MODE
        if (ChirpSetPower(0))
            ESP_LOGI(TAG, "Power set: 22dBm (Dev mode)");
        else
            ESP_LOGW(TAG, "Power set failed");

        // if (ChirpSetPower(3))
        //     ESP_LOGI(TAG, "Power set: 14dBm (Dev mode)");
        // else
        //     ESP_LOGW(TAG, "Power set failed");
#endif
    }
    
    ChirpSetMode(0, 0);

    // 创建发送任务（优先级 4）
    BaseType_t ret = xTaskCreate(ChirpChannelTask, "chirp_ch_task",
                                 3072, NULL, 4, NULL);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create Chirp TX task");
        return ESP_FAIL;
    }

    // 创建接收任务（优先级 6，高于应用层其它任务）
    ret = xTaskCreate(ChirpRxTask, "chirp_rx_task",
                      4096, NULL, 6, NULL);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create Chirp RX task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

uint8_t ChirpGetChannel(void)
{
    return s_channel;
}

bool ChirpIsReady(void)
{
    return s_ready;
}


/* ===== 设置发射功率（读改写 REG1 bit1~0） =====
 * power 取值：
 *   0 = 22 dBm（默认，最大）
 *   1 = 20 dBm
 *   2 = 17 dBm
 *   3 = 14 dBm（最小）
 * 返回 1=成功  0=失败
 *
 * 注意：需在配置模式下调用
 */
uint8_t ChirpSetPower(uint8_t power)
{
    if (power > 3)
    {
        ESP_LOGW(TAG, "Power invalid: %d (must be 0~3)", power);
        return 0;
    }

    uint8_t resp[8] = {0};
    int len;

    /* 读 REG1（地址 0x04） */
    uint8_t rcmd[] = { 0xC1, 0x04, 0x01 };
    uart_flush_input(CHIRP_UART_NUM);
    uart_write_bytes(CHIRP_UART_NUM, rcmd, sizeof(rcmd));
    len = uart_read_bytes(CHIRP_UART_NUM, resp, sizeof(resp),
                          pdMS_TO_TICKS(200));

    if (len < 4 || resp[0] != 0xC1 || resp[1] != 0x04 || resp[2] != 0x01)
    {
        ESP_LOGW(TAG, "Read REG1 failed (len=%d)", len);
        return 0;
    }

    uint8_t reg1 = resp[3];
    reg1 = (reg1 & 0xFC) | (power & 0x03);

    /* 写回 REG1 */
    uint8_t wcmd[] = { 0xC0, 0x04, 0x01, reg1 };
    uart_flush_input(CHIRP_UART_NUM);
    uart_write_bytes(CHIRP_UART_NUM, wcmd, sizeof(wcmd));
    len = uart_read_bytes(CHIRP_UART_NUM, resp, sizeof(resp),
                          pdMS_TO_TICKS(200));

    if (len < 4 || resp[0] != 0xC1 || resp[1] != 0x04 || resp[2] != 0x01)
    {
        ESP_LOGW(TAG, "Write REG1 failed (len=%d)", len);
        return 0;
    }

    ESP_LOGI(TAG, "Power set OK (0x%02X)", resp[3]);
    return 1;
}

/* ===== 启用定点传输（读改写 REG3 bit6 = 1） =====
 * 返回 1=成功  0=失败
 *
 * 注意：需在配置模式下调用
 */
uint8_t ChirpEnableFixedPoint(void)
{
    uint8_t resp[8] = {0};
    int len;

    /* 读 REG3（地址 0x06） */
    uint8_t rcmd[] = { 0xC1, 0x06, 0x01 };
    uart_flush_input(CHIRP_UART_NUM);
    uart_write_bytes(CHIRP_UART_NUM, rcmd, sizeof(rcmd));
    len = uart_read_bytes(CHIRP_UART_NUM, resp, sizeof(resp),
                          pdMS_TO_TICKS(200));

    if (len < 4 || resp[0] != 0xC1 || resp[1] != 0x06 || resp[2] != 0x01)
    {
        ESP_LOGW(TAG, "Read REG3 failed (len=%d)", len);
        return 0;
    }

    uint8_t reg3 = resp[3];
    reg3 |= 0x40;   /* bit6 = 1 */

    /* 写回 REG3 */
    uint8_t wcmd[] = { 0xC0, 0x06, 0x01, reg3 };
    uart_flush_input(CHIRP_UART_NUM);
    uart_write_bytes(CHIRP_UART_NUM, wcmd, sizeof(wcmd));
    len = uart_read_bytes(CHIRP_UART_NUM, resp, sizeof(resp),
                          pdMS_TO_TICKS(200));

    if (len < 4 || resp[0] != 0xC1 || resp[1] != 0x06 || resp[2] != 0x01)
    {
        ESP_LOGW(TAG, "Write REG3 failed (len=%d)", len);
        return 0;
    }

    ESP_LOGI(TAG, "REG3 set OK (0x%02X)", resp[3]);
    return 1;
}