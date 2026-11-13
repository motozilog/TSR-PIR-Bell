#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "cJSON.h"

#include "bsp_led.h"
#include "bsp_key.h"
#include "bsp_wifiap.h"
#include "bsp_wifistation.h"
#include "bsp_webserver.h"
#include "bsp_littlefs.h"
#include "bsp_es8311.h"
#include "bsp_player.h"
#include "bsp_chirp.h"
#include "bsp_ws2812.h"
#include "bsp_led_status.h"
#include "bsp_ntp.h"
#include "bsp_battery.h"

#include "app_config.h"
#include "app_chirp.h"
#include "app_ip_announce.h"

#include "esp_partition.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"

static const char *TAG = "MAIN";

static void key_task(void *args)
{
    (void)args;
    KeyGpioConfig();
    bool last_pressed = false;
    int press_count = 0;
    bool reset_done = false;
    int64_t start_us = esp_timer_get_time();

    while (1)
    {
        int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
        bool window_open = (elapsed_ms <= 60000);

        bool now_pressed = (GetKeyValue() == 0);
        if (now_pressed && !last_pressed)
        {
            LedOn();
            vTaskDelay(100 / portTICK_PERIOD_MS);
            LedOff();

            ESP_LOGI(TAG, "[key] pressed, trigger play (KEY priority)");
            PlayerTryPlay(NULL, PLAYER_OWNER_KEY, 0, 0);

            if (window_open && !reset_done)
            {
                press_count++;
                ESP_LOGI(TAG, "[reset] press %d/5, elapsed=%lld ms",
                         press_count, (long long)elapsed_ms);

                if (press_count >= 5)
                {
                    ESP_LOGI(TAG, "[reset] ===== RESET TRIGGERED =====");
                    AppConfig_ResetToDefault();
                    ESP_LOGI(TAG, "[reset] Config reset to default");
                    reset_done = true;

                    vTaskDelay(500 / portTICK_PERIOD_MS);
                    esp_restart();
                }
            }
        }
        last_pressed = now_pressed;
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    /* 打印 PSRAM 状态，确认是否启用成功 */
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (psram_total > 0) {
        ESP_LOGI(TAG, "PSRAM enabled: total=%u KB, free=%u KB", 
                 (unsigned)(psram_total / 1024), (unsigned)(psram_free / 1024));
    } else {
        ESP_LOGW(TAG, "PSRAM NOT enabled! (total size is 0)");
    }

    LedGpioConfig();

    if (WS2812_Init() != ESP_OK)
        ESP_LOGE(TAG, "WS2812 init failed");

    LittleFsInit();

    AppConfig_Init();

    if (Es8311AudioInit() != ESP_OK)
        ESP_LOGE(TAG, "audio init failed");

    if (Es8311IsReady()) {
        SysConfig_t *cfg_vol = AppConfig_Get();
        uint8_t vol = cfg_vol->volume ? cfg_vol->volume : 70;
        if (vol < 10) vol = 10;
        if (vol > 100) vol = 100;
        Es8311SetVolume(vol);
    }

    if (Es8311IsReady())
        PlayerInit();

    /* IP 播报任务：需在 PlayerInit 之后，WiFi 连接成功时由 bsp_wifistation 置标志触发 */
    AppIpAnnounceInit();

    xTaskCreate(key_task, "key_task", 4096, NULL, 5, NULL);

    SysConfig_t *cfg_boot = AppConfig_Get();
    vTaskDelay(500 / portTICK_PERIOD_MS);
    if (cfg_boot->boot_play)
    {
        ESP_LOGI(TAG, "startup audio: %s", PLAYER_DEFAULT_FILE);
        PlayerTryPlay(PLAYER_DEFAULT_FILE, PLAYER_OWNER_KEY, 0, 0);
    }
    else
    {
        ESP_LOGI(TAG, "startup audio disabled by config");
    }

    if (ChirpInit() != ESP_OK)
    {
        ESP_LOGE(TAG, "Chirp init failed");
    }

    AppChirp_Init();

    WifiCommonInit();

    SysConfig_t *cfg = AppConfig_Get();

    if (cfg->wifi_mode == 1)
    {
        /* Station 模式：无限重连，永不回退 AP（除非参数非法兜底） */
        if (cfg->ssid[0] == '\0') {
            ESP_LOGW(TAG, "Station mode but SSID empty, fallback to AP");
            WifiApStart();
        } else {
            const char *ip_mode_str = (cfg->ip_mode == 1) ? "static" : "dhcp";

            ESP_LOGI(TAG, "Config mode=station, connecting to %s (ip_mode=%s)",
                     cfg->ssid, ip_mode_str);

            esp_err_t ret = WifiStationStart(cfg->ssid, cfg->password,
                                             ip_mode_str,
                                             cfg->ip, cfg->netmask, cfg->gateway,
                                             cfg->dns1, cfg->dns2);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "WifiStationStart failed: %s, fallback to AP",
                         esp_err_to_name(ret));
                WifiApStart();
            } else {
                /* 不阻塞等待连接结果，NTP 守护任务会等 WiFi 连上后自动启动 SNTP；
                 * 断线后由 wifi_reconnect 任务无限重试 */
                NtpInit();
                ESP_LOGI(TAG, "Station started (infinite reconnect enabled)");
            }
        }
    }
    else
    {
        ESP_LOGI(TAG, "Config mode=ap, starting AP");
        WifiApStart();
    }

    WebServerInit();

    /* LED 状态机 + 开机查询外机电量 */
    LedStatusInit();

    /* 电池采样任务：启动 1 分钟后开始，每 30 秒采一次 */
    BatteryInit();    

    ESP_LOGI(TAG, "TSR PIR Bell By motozilog V1.0 compile time: %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "app_main done");
}