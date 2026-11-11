#ifndef _BSP_WIFISTATION_H_
#define _BSP_WIFISTATION_H_

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/**
 * @函数说明        启动 Station 模式
 * @传入参数        ssid       WiFi 名称
 * @传入参数        password   WiFi 密码
 * @传入参数        ip_mode    "dhcp" 或 "static"
 * @传入参数        ip         静态IP（ip_mode=static 时使用，否则传 NULL）
 * @传入参数        netmask    掩码（同上）
 * @传入参数        gateway    网关（同上）
 * @传入参数        dns1       DNS主（同上）
 * @传入参数        dns2       DNS备（同上）
 * @函数返回        ESP_OK=已发起连接（不代表连接成功）
 */
esp_err_t WifiStationStart(const char *ssid, const char *password,
                           const char *ip_mode,
                           const char *ip, const char *netmask,
                           const char *gateway,
                           const char *dns1, const char *dns2);

bool WifiStationIsConnected(void);
bool WifiStationIsConnecting(void);
EventGroupHandle_t WifiStationGetEventGroup(void);

#endif