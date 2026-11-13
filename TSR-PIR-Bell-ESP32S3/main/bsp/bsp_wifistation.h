#ifndef _BSP_WIFISTATION_H_
#define _BSP_WIFISTATION_H_

#include <stdbool.h>
#include <stddef.h>
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
 *
 * 注意：可重复调用（netif 与事件 handler 已存在则复用），
 *       用于配置保存前试连新参数。
 *       断线后由内部重连任务无限重试（指数退避 1s~30s）。
 */
esp_err_t WifiStationStart(const char *ssid, const char *password,
                           const char *ip_mode,
                           const char *ip, const char *netmask,
                           const char *gateway,
                           const char *dns1, const char *dns2);

/* 停止 Station（只 esp_wifi_stop，保留 netif 和事件 handler，供下次 Start 复用） */
void WifiStationStop(void);

/* 等 STA_CONNECTED_BIT（GOT_IP）或 STA_FAIL_BIT，timeout_ms 超时返回 false
 * 注意：无限重连模式下 STA_FAIL_BIT 不再置位，此函数仅用于配置试连的超时等待 */
bool WifiStationWaitConnected(uint32_t timeout_ms);

bool WifiStationIsConnected(void);
bool WifiStationIsConnecting(void);

/* 是否处于"重连中"（已发起过连接、当前未连上、重连任务仍在跑） */
bool WifiStationIsReconnecting(void);

EventGroupHandle_t WifiStationGetEventGroup(void);

/* 取当前 STA IP 字符串（点分十进制），未连接返回空串 */
void WifiStationGetIpString(char *buf, size_t len);

/* 取出"待播报 IP"（IP 变化后置位）。有则拷贝到 buf 并清标志，返回 true；
 * 无待播返回 false。供 app_ip_announce 轮询消费。 */
bool WifiStationTakePendingIp(char *buf, size_t len);

#endif