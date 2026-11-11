#include "bsp_wifistation.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "lwip/ip_addr.h"
#include <string.h>

static const char *TAG = "BSP_WIFISTA";

static esp_netif_t *s_sta_netif = NULL;
static EventGroupHandle_t s_sta_event_group = NULL;
static bool s_connected = false;
static bool s_connecting = false;

/* 事件位 */
#define STA_CONNECTED_BIT    BIT0
#define STA_FAIL_BIT         BIT1

/* 最大重连次数 */
#define STA_MAX_RETRY        5

static int s_retry_num = 0;

/* ===== 事件处理 ===== */
static void WifiStaEventHandler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started, connecting...");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "STA connected to AP");
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
            {
                wifi_event_sta_disconnected_t *event =
                    (wifi_event_sta_disconnected_t *)event_data;
                s_connected = false;

                if (s_retry_num < STA_MAX_RETRY)
                {
                    ESP_LOGW(TAG, "Disconnected (reason %d), retry %d/%d",
                             event->reason, s_retry_num + 1, STA_MAX_RETRY);
                    esp_wifi_connect();
                    s_retry_num++;
                }
                else
                {
                    ESP_LOGE(TAG, "STA failed after %d retries, giving up", STA_MAX_RETRY);
                    s_connecting = false;
                    xEventGroupSetBits(s_sta_event_group, STA_FAIL_BIT);
                }
                break;
            }

            default:
                break;
        }
    }
    else if (event_base == IP_EVENT)
    {
        if (event_id == IP_EVENT_STA_GOT_IP)
        {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            s_connected = true;
            s_connecting = false;
            s_retry_num = 0;
            xEventGroupSetBits(s_sta_event_group, STA_CONNECTED_BIT);
        }
    }
}


/**
 * @函数说明        配置静态 IP 和 DNS
 */
static void WifiStationConfigureStaticIp(const char *ip, const char *netmask,
                                          const char *gateway,
                                          const char *dns1, const char *dns2)
{
    // 1. 停止 DHCP 客户端
    esp_netif_dhcpc_stop(s_sta_netif);

    // 2. 设置静态 IP
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    ip_info.ip.addr      = ipaddr_addr(ip);
    ip_info.netmask.addr = ipaddr_addr(netmask);
    ip_info.gw.addr      = ipaddr_addr(gateway);

    esp_err_t ret = esp_netif_set_ip_info(s_sta_netif, &ip_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set static IP failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Static IP configured: " IPSTR "/" IPSTR ", gw " IPSTR,
             IP2STR(&ip_info.ip), IP2STR(&ip_info.netmask), IP2STR(&ip_info.gw));

    // 3. 设置 DNS1
    if (dns1 && dns1[0] != '\0') {
        esp_netif_dns_info_t dns;
        memset(&dns, 0, sizeof(dns));
        dns.ip.u_addr.ip4.addr = ipaddr_addr(dns1);
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns);
        ESP_LOGI(TAG, "DNS main: %s", dns1);
    }

    // 4. 设置 DNS2（备用）
    if (dns2 && dns2[0] != '\0') {
        esp_netif_dns_info_t dns;
        memset(&dns, 0, sizeof(dns));
        dns.ip.u_addr.ip4.addr = ipaddr_addr(dns2);
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_BACKUP, &dns);
        ESP_LOGI(TAG, "DNS backup: %s", dns2);
    }
}

/* ===== 启动 Station ===== */
esp_err_t WifiStationStart(const char *ssid, const char *password,
                           const char *ip_mode,
                           const char *ip, const char *netmask,
                           const char *gateway,
                           const char *dns1, const char *dns2)
{
    if (ssid == NULL || ssid[0] == '\0')
    {
        ESP_LOGE(TAG, "SSID is empty");
        return ESP_ERR_INVALID_ARG;
    }

    // 创建事件组
    if (s_sta_event_group == NULL)
    {
        s_sta_event_group = xEventGroupCreate();
    }
    else
    {
        xEventGroupClearBits(s_sta_event_group, STA_CONNECTED_BIT | STA_FAIL_BIT);
    }

    // 创建 STA netif
    s_sta_netif = esp_netif_create_default_wifi_sta();

    // === 先配置 IP（必须在 esp_wifi_start 之前）===
    if (ip_mode && strcmp(ip_mode, "static") == 0)
    {
        if (ip && ip[0] && netmask && netmask[0] && gateway && gateway[0])
        {
            WifiStationConfigureStaticIp(ip, netmask, gateway, dns1, dns2);
        }
        else
        {
            ESP_LOGW(TAG, "Static IP mode but fields empty, fallback to DHCP");
        }
    }
    else
    {
        ESP_LOGI(TAG, "Using DHCP");
    }

    // 注册事件
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &WifiStaEventHandler,
                                                         NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         &WifiStaEventHandler,
                                                         NULL, NULL));

    // 配置 STA
    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, password ? password : "",
            sizeof(sta_config.sta.password) - 1);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_connecting = true;
    ESP_LOGI(TAG, "Station started, connecting to %s...", ssid);
    return ESP_OK;
}

bool WifiStationIsConnected(void)
{
    return s_connected;
}

bool WifiStationIsConnecting(void)
{
    return s_connecting;
}

EventGroupHandle_t WifiStationGetEventGroup(void)
{
    return s_sta_event_group;
}