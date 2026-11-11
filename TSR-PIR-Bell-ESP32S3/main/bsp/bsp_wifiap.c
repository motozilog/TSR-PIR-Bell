#include "bsp_wifiap.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/ip_addr.h"
#include "bsp_led.h"
#include "esp_mac.h"
#include "dhcpserver/dhcpserver.h"

static const char *TAG = "BSP_WIFIAP";

static esp_netif_t *s_ap_netif = NULL;
static EventGroupHandle_t s_wifi_event_group = NULL;
static bool s_common_inited = false;

// ===== 公共基础设施初始化 =====
void WifiCommonInit(void)
{
    if (s_common_inited) return;

    // 1. NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 事件组
    s_wifi_event_group = xEventGroupCreate();

    // 3. netif + event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 4. WiFi 驱动
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_common_inited = true;
    ESP_LOGI(TAG, "Common WiFi infrastructure initialized");
}

// ===== AP 事件回调（保持原有） =====
static void WifiApEventHandler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "AP started, SSID: %s", WIFIAP_SSID);
                break;

            case WIFI_EVENT_AP_STACONNECTED:
            {
                wifi_event_ap_staconnected_t *event =
                    (wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG, "Station connected, MAC: " MACSTR, MAC2STR(event->mac));
                xEventGroupSetBits(s_wifi_event_group, WIFIAP_STA_CONNECTED_BIT);
                break;
            }

            case WIFI_EVENT_AP_STADISCONNECTED:
            {
                wifi_event_ap_stadisconnected_t *event =
                    (wifi_event_ap_stadisconnected_t *)event_data;
                ESP_LOGI(TAG, "Station disconnected, MAC: " MACSTR, MAC2STR(event->mac));
                xEventGroupSetBits(s_wifi_event_group, WIFIAP_STA_DISCONNECTED_BIT);
                break;
            }

            default:
                break;
        }
    }
}

// ===== AP 静态 IP 配置（保持原有） =====
static void WifiApConfigureStaticIp(void)
{
    esp_netif_dhcps_stop(s_ap_netif);

    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));
    ip_info.ip.addr      = ipaddr_addr(WIFIAP_IP_ADDR);
    ip_info.netmask.addr = ipaddr_addr(WIFIAP_NETMASK);
    ip_info.gw.addr      = ipaddr_addr(WIFIAP_GATEWAY);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ip_info));

    esp_netif_dns_info_t dns_info;
    memset(&dns_info, 0, sizeof(esp_netif_dns_info_t));
    dns_info.ip.u_addr.ip4.addr = ipaddr_addr(WIFIAP_DNS_ADDR);
    dns_info.ip.type            = ESP_IPADDR_TYPE_V4;
    ESP_ERROR_CHECK(esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns_info));

    ESP_ERROR_CHECK(esp_netif_dhcps_start(s_ap_netif));
    ESP_LOGI(TAG, "Static IP configured: %s", WIFIAP_IP_ADDR);
}

// ===== 启动 AP 模式 =====
void WifiApStart(void)
{
    // 创建 AP netif
    s_ap_netif = esp_netif_create_default_wifi_ap();
    WifiApConfigureStaticIp();

    // 注册 AP 事件处理
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &WifiApEventHandler,
                                                         NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFIAP_SSID,
            .ssid_len = strlen(WIFIAP_SSID),
            .password = WIFIAP_PASSWORD,
            .channel = WIFIAP_CHANNEL,
            .max_connection = WIFIAP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    // 改为 APSTA 模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi APSTA started successfully");
}

// ===== 保留旧接口（向后兼容） =====
void WifiApInit(void)
{
    WifiCommonInit();
    WifiApStart();
}

EventGroupHandle_t WifiApGetEventGroup(void)
{
    return s_wifi_event_group;
}

// LedBlinkTask 保持不变
/**
 * @函数说明        LED 闪烁任务
 *                  监听 WiFi 连接状态，有客户端时闪烁
 */
void LedBlinkTask(void *pvParameters)
{
    (void)pvParameters;
    EventGroupHandle_t event_group = WifiApGetEventGroup();
    bool led_state = false;
    bool client_connected = false;

    while (1)
    {
        // 检查是否有客户端连接
        EventBits_t bits = xEventGroupGetBits(event_group);

        if (bits & WIFIAP_STA_CONNECTED_BIT)
        {
            client_connected = true;
        }
        else if (bits & WIFIAP_STA_DISCONNECTED_BIT)
        {
            client_connected = false;
        }

        if (client_connected)
        {
            // 有客户端连接，闪烁 LED
            led_state = !led_state;
            if (led_state)
                LedOn();
            else
                LedOff();
        }
        else
        {
            // 没有客户端，LED 熄灭
            LedOff();
        }

        vTaskDelay(500 / portTICK_PERIOD_MS);  // 500ms = 0.5s
    }
}