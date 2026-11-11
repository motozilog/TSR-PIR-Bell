#ifndef _BSP_WIFIAP_H_
#define _BSP_WIFIAP_H_

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

// AP 配置参数（不变）
#define WIFIAP_SSID         "TSR-PIR-Bell"
#define WIFIAP_PASSWORD     ""
#define WIFIAP_CHANNEL      1
#define WIFIAP_MAX_CONN     4

#define WIFIAP_IP_ADDR      "192.168.5.1"
#define WIFIAP_NETMASK      "255.255.255.0"
#define WIFIAP_GATEWAY      "192.168.5.1"

#define WIFIAP_DNS_ADDR     "192.168.5.1"

#define WIFIAP_STA_CONNECTED_BIT    BIT0
#define WIFIAP_STA_DISCONNECTED_BIT BIT1

// 公共基础设施初始化（NVS + netif + event loop + WiFi 驱动）
// 只调用一次，AP 和 Station 共用
void WifiCommonInit(void);

// 启动 AP 模式
void WifiApStart(void);

// 保留旧接口（内部调用 WifiCommonInit + WifiApStart）
void WifiApInit(void);

EventGroupHandle_t WifiApGetEventGroup(void);
void LedBlinkTask(void *pvParameters);

#endif