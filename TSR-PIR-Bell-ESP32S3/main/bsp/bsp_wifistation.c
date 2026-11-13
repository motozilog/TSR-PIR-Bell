#include "bsp_wifistation.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "lwip/ip_addr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "BSP_WIFISTA";

static esp_netif_t *s_sta_netif = NULL;
static EventGroupHandle_t s_sta_event_group = NULL;
static bool s_connected = false;
static bool s_connecting = false;

/* 事件 handler 只注册一次 */
static bool s_handler_registered = false;

/* 当前 IP 字符串（点分十进制） */
static char s_sta_ip_str[16] = {0};

/* 上次已播报的 IP（去重） */
static char s_last_announced_ip[16] = {0};

/* 待播报标志：GOT_IP 且与上次不同则置位 */
static volatile bool s_ip_announce_pending = false;

/* 无限重连标志：Start 后置 true，Stop 时清 false */
static volatile bool s_reconnect_enabled = false;

/* 重连退避：当前延迟 ms（1s 起，翻倍，30s 封顶） */
static volatile uint32_t s_retry_delay_ms = 1000;

/* 事件位 */
#define STA_CONNECTED_BIT    BIT0
#define STA_FAIL_BIT         BIT1

/* 退避参数 */
#define STA_RETRY_MIN_MS     1000
#define STA_RETRY_MAX_MS     30000

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

                /* 无限重连：不在回调里直接 esp_wifi_connect（避免驱动快速循环），
                 * 由 wifi_reconnect_task 按退避间隔发起 */
                ESP_LOGW(TAG, "Disconnected (reason %d), will reconnect (enabled=%d)",
                         event->reason, (int)s_reconnect_enabled);
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
            s_retry_delay_ms = STA_RETRY_MIN_MS;   /* 连上后重置退避 */

            snprintf(s_sta_ip_str, sizeof(s_sta_ip_str),
                     IPSTR, IP2STR(&event->ip_info.ip));

            if (strcmp(s_sta_ip_str, s_last_announced_ip) != 0) {
                strncpy(s_last_announced_ip, s_sta_ip_str,
                        sizeof(s_last_announced_ip) - 1);
                s_last_announced_ip[sizeof(s_last_announced_ip) - 1] = '\0';
                s_ip_announce_pending = true;
                ESP_LOGI(TAG, "IP announce pending: %s", s_sta_ip_str);
            } else {
                ESP_LOGI(TAG, "IP unchanged (%s), skip announce", s_sta_ip_str);
            }

            xEventGroupSetBits(s_sta_event_group, STA_CONNECTED_BIT);
        }
    }
}

/* ===== 无限重连任务：按指数退避发起 esp_wifi_connect ===== */
static void WifiReconnectTask(void *arg)
{
    (void)arg;

    while (1) {
        if (!s_reconnect_enabled || s_connected) {
            /* 未启用重连或已连上，休息一会再查 */
            s_retry_delay_ms = STA_RETRY_MIN_MS;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        /* 等待退避时间 */
        uint32_t delay_ms = s_retry_delay_ms;
        ESP_LOGI(TAG, "Reconnect in %u ms", (unsigned)delay_ms);

        /* 分段延时，期间若连上或 Stop 则提前退出 */
        uint32_t waited = 0;
        while (waited < delay_ms) {
            if (s_connected || !s_reconnect_enabled) break;
            vTaskDelay(pdMS_TO_TICKS(100));
            waited += 100;
        }
        if (s_connected || !s_reconnect_enabled) continue;

        ESP_LOGI(TAG, "Reconnect attempt (delay was %u ms)", (unsigned)delay_ms);
        esp_wifi_connect();

        /* 退避翻倍，30s 封顶 */
        s_retry_delay_ms = (s_retry_delay_ms >= STA_RETRY_MAX_MS / 2)
                           ? STA_RETRY_MAX_MS
                           : s_retry_delay_ms * 2;

        /* 给驱动一点时间尝试，再判断是否还需要重连 */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @函数说明        配置静态 IP 和 DNS
 */
static void WifiStationConfigureStaticIp(const char *ip, const char *netmask,
                                          const char *gateway,
                                          const char *dns1, const char *dns2)
{
    esp_netif_dhcpc_stop(s_sta_netif);

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

    if (dns1 && dns1[0] != '\0') {
        esp_netif_dns_info_t dns;
        memset(&dns, 0, sizeof(dns));
        dns.ip.u_addr.ip4.addr = ipaddr_addr(dns1);
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns);
        ESP_LOGI(TAG, "DNS main: %s", dns1);
    }

    if (dns2 && dns2[0] != '\0') {
        esp_netif_dns_info_t dns;
        memset(&dns, 0, sizeof(dns));
        dns.ip.u_addr.ip4.addr = ipaddr_addr(dns2);
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_BACKUP, &dns);
        ESP_LOGI(TAG, "DNS backup: %s", dns2);
    }
}

/* ===== 启动 Station（可重复调用） ===== */
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

    if (s_sta_event_group == NULL)
    {
        s_sta_event_group = xEventGroupCreate();
    }
    xEventGroupClearBits(s_sta_event_group, STA_CONNECTED_BIT | STA_FAIL_BIT);

    /* netif 已存在则复用，避免重复创建 */
    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (s_sta_netif == NULL) {
            s_sta_netif = esp_netif_create_default_wifi_sta();
            ESP_LOGI(TAG, "STA netif created");
        } else {
            ESP_LOGI(TAG, "STA netif reused");
        }
    }

    /* 静态 IP（必须在 esp_wifi_start 之前） */
    if (ip_mode && strcmp(ip_mode, "static") == 0)
    {
        if (ip && ip[0] && netmask && netmask[0] && gateway && gateway[0])
        {
            WifiStationConfigureStaticIp(ip, netmask, gateway, dns1, dns2);
        }
        else
        {
            ESP_LOGW(TAG, "Static IP mode but fields empty, fallback to DHCP");
            esp_netif_dhcpc_start(s_sta_netif);
        }
    }
    else
    {
        esp_netif_dhcpc_start(s_sta_netif);
        ESP_LOGI(TAG, "Using DHCP");
    }

    /* 事件 handler 只注册一次 */
    if (!s_handler_registered) {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                             ESP_EVENT_ANY_ID,
                                                             &WifiStaEventHandler,
                                                             NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                             IP_EVENT_STA_GOT_IP,
                                                             &WifiStaEventHandler,
                                                             NULL, NULL));
        s_handler_registered = true;
    }

    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, password ? password : "",
            sizeof(sta_config.sta.password) - 1);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    /* 已启动则先停再起，确保用新配置重新连接 */
    wifi_mode_t cur_mode;
    if (esp_wifi_get_mode(&cur_mode) == ESP_OK) {
        esp_wifi_stop();
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    s_connecting = true;
    s_connected  = false;
    s_retry_delay_ms   = STA_RETRY_MIN_MS;
    s_reconnect_enabled = true;

    /* 首次启动重连任务（全局一个） */
    static bool s_reconnect_task_created = false;
    if (!s_reconnect_task_created) {
        BaseType_t ok = xTaskCreate(WifiReconnectTask, "wifi_reconnect",
                                    3072, NULL, 4, NULL);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "create wifi_reconnect task failed");
        } else {
            s_reconnect_task_created = true;
        }
    }

    ESP_LOGI(TAG, "Station started, connecting to %s...", ssid);
    return ESP_OK;
}

void WifiStationStop(void)
{
    ESP_LOGI(TAG, "Stopping station");
    s_connected  = false;
    s_connecting = false;
    s_reconnect_enabled = false;
    s_retry_delay_ms = STA_RETRY_MIN_MS;
    if (s_sta_event_group) {
        xEventGroupClearBits(s_sta_event_group, STA_CONNECTED_BIT | STA_FAIL_BIT);
    }
    esp_wifi_stop();
}

bool WifiStationWaitConnected(uint32_t timeout_ms)
{
    if (s_sta_event_group == NULL) return false;
    EventBits_t bits = xEventGroupWaitBits(s_sta_event_group,
                                           STA_CONNECTED_BIT | STA_FAIL_BIT,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & STA_CONNECTED_BIT) != 0;
}

bool WifiStationIsConnected(void)
{
    return s_connected;
}

bool WifiStationIsConnecting(void)
{
    return s_connecting;
}

bool WifiStationIsReconnecting(void)
{
    return (s_reconnect_enabled && !s_connected);
}

EventGroupHandle_t WifiStationGetEventGroup(void)
{
    return s_sta_event_group;
}

void WifiStationGetIpString(char *buf, size_t len)
{
    if (buf == NULL || len == 0) return;
    if (!s_connected) {
        buf[0] = '\0';
        return;
    }
    strncpy(buf, s_sta_ip_str, len - 1);
    buf[len - 1] = '\0';
}

bool WifiStationTakePendingIp(char *buf, size_t len)
{
    if (buf == NULL || len == 0) return false;
    if (!s_ip_announce_pending) return false;

    strncpy(buf, s_sta_ip_str, len - 1);
    buf[len - 1] = '\0';
    s_ip_announce_pending = false;
    return (buf[0] != '\0');
}