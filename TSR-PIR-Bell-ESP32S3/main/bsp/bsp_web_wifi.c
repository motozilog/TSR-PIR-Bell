#include "bsp_webserver.h"
#include "app_config.h"
#include "bsp_led_status.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "lwip/ip_addr.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "BSP_WEB_WIFI";

bool GetStaActiveIpInfo(esp_netif_ip_info_t *ip_info,
                        esp_ip4_addr_t *dns1,
                        esp_ip4_addr_t *dns2)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) return false;

    if (esp_netif_get_ip_info(netif, ip_info) != ESP_OK) return false;
    if (ip_info->ip.addr == 0) return false;

    esp_netif_dns_info_t dns;
    if (dns1) {
        if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
            dns1->addr = dns.ip.u_addr.ip4.addr;
        }
    }
    if (dns2) {
        if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns) == ESP_OK) {
            dns2->addr = dns.ip.u_addr.ip4.addr;
        }
    }
    return true;
}

static esp_err_t ApiWifiConfigGetHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    SysConfig_t *cfg = AppConfig_Get();

    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "mode", cfg->wifi_mode == 0 ? "ap" : "station");
    cJSON_AddStringToObject(out, "ssid", cfg->ssid);
    cJSON_AddBoolToObject(out, "has_password", cfg->password[0] != '\0');

    cJSON_AddStringToObject(out, "ip_mode", cfg->ip_mode == 0 ? "dhcp" : "static");
    cJSON_AddStringToObject(out, "ip",      cfg->ip);
    cJSON_AddStringToObject(out, "netmask", cfg->netmask);
    cJSON_AddStringToObject(out, "gateway", cfg->gateway);
    cJSON_AddStringToObject(out, "dns1",    cfg->dns1);
    cJSON_AddStringToObject(out, "dns2",    cfg->dns2);
    cJSON_AddStringToObject(out, "ntp_server",
        cfg->ntp_server[0] ? cfg->ntp_server : "ntp.aliyun.com");
    cJSON_AddNumberToObject(out, "led_normal_on", cfg->led_normal_on ? 1 : 0);
    cJSON_AddNumberToObject(out, "led_brightness", cfg->led_brightness ? cfg->led_brightness : 50);

    if (cfg->wifi_mode == 1 && cfg->ip_mode == 0)
    {
        esp_netif_ip_info_t actual;
        esp_ip4_addr_t adns1 = {0}, adns2 = {0};
        if (GetStaActiveIpInfo(&actual, &adns1, &adns2))
        {
            char buf2[32];
            snprintf(buf2, sizeof(buf2), IPSTR, IP2STR(&actual.ip));
            cJSON_AddStringToObject(out, "actual_ip", buf2);

            snprintf(buf2, sizeof(buf2), IPSTR, IP2STR(&actual.netmask));
            cJSON_AddStringToObject(out, "actual_netmask", buf2);

            snprintf(buf2, sizeof(buf2), IPSTR, IP2STR(&actual.gw));
            cJSON_AddStringToObject(out, "actual_gateway", buf2);

            if (adns1.addr != 0) {
                snprintf(buf2, sizeof(buf2), IPSTR, IP2STR(&adns1));
                cJSON_AddStringToObject(out, "actual_dns1", buf2);
            }
            if (adns2.addr != 0) {
                snprintf(buf2, sizeof(buf2), IPSTR, IP2STR(&adns2));
                cJSON_AddStringToObject(out, "actual_dns2", buf2);
            }
        }
    }

    char *out_str = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);

    ESP_LOGI(TAG, "Read wifi config: %s", out_str);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out_str, HTTPD_RESP_USE_STRLEN);
    free(out_str);
    return ESP_OK;
}

static esp_err_t ApiWifiConfigPostHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    char buf[768] = {0};

    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    SysConfig_t *cfg = AppConfig_Get();

    cJSON *mode = cJSON_GetObjectItem(root, "mode");
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass = cJSON_GetObjectItem(root, "password");
    cJSON *ip_mode = cJSON_GetObjectItem(root, "ip_mode");
    cJSON *ip      = cJSON_GetObjectItem(root, "ip");
    cJSON *netmask = cJSON_GetObjectItem(root, "netmask");
    cJSON *gateway = cJSON_GetObjectItem(root, "gateway");
    cJSON *dns1    = cJSON_GetObjectItem(root, "dns1");
    cJSON *dns2    = cJSON_GetObjectItem(root, "dns2");
    cJSON *led_on  = cJSON_GetObjectItem(root, "led_normal_on");
    cJSON *led_bri = cJSON_GetObjectItem(root, "led_brightness");

    if (!cJSON_IsString(mode))
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mode");
        return ESP_FAIL;
    }

    cfg->wifi_mode = (strcmp(mode->valuestring, "station") == 0) ? 1 : 0;

    if (cJSON_IsString(ssid))
        strncpy(cfg->ssid, ssid->valuestring, sizeof(cfg->ssid) - 1);

    if (cJSON_IsString(pass) && pass->valuestring[0] != '\0')
    {
        strncpy(cfg->password, pass->valuestring, sizeof(cfg->password) - 1);
    }

    if (cJSON_IsString(ip_mode))
        cfg->ip_mode = (strcmp(ip_mode->valuestring, "static") == 0) ? 1 : 0;

    if (cJSON_IsString(ip))
        strncpy(cfg->ip, ip->valuestring, sizeof(cfg->ip) - 1);
    if (cJSON_IsString(netmask))
        strncpy(cfg->netmask, netmask->valuestring, sizeof(cfg->netmask) - 1);
    if (cJSON_IsString(gateway))
        strncpy(cfg->gateway, gateway->valuestring, sizeof(cfg->gateway) - 1);
    if (cJSON_IsString(dns1))
        strncpy(cfg->dns1, dns1->valuestring, sizeof(cfg->dns1) - 1);
    if (cJSON_IsString(dns2))
        strncpy(cfg->dns2, dns2->valuestring, sizeof(cfg->dns2) - 1);

    if (cJSON_IsNumber(led_on))
        cfg->led_normal_on = (led_on->valueint != 0) ? 1 : 0;

    if (cJSON_IsNumber(led_bri))
    {
        int v = led_bri->valueint;
        if (v < 10) v = 10;
        if (v > 100) v = 100;
        cfg->led_brightness = (uint8_t)v;
    }

    cJSON *ntp_item = cJSON_GetObjectItem(root, "ntp_server");
    char new_ntp[64];
    if (cJSON_IsString(ntp_item) && ntp_item->valuestring[0] != '\0') {
        strncpy(new_ntp, ntp_item->valuestring, sizeof(new_ntp) - 1);
    } else {
        strncpy(new_ntp, "ntp.aliyun.com", sizeof(new_ntp) - 1);
    }
    new_ntp[sizeof(new_ntp) - 1] = '\0';

    cJSON_Delete(root);

    /* 不验证 NTP，直接保存 */
    ESP_LOGI(TAG, "Save NTP server without verify: %s (mode=%s)",
             new_ntp, cfg->wifi_mode == 1 ? "station" : "ap");

    strncpy(cfg->ntp_server, new_ntp, sizeof(cfg->ntp_server) - 1);
    cfg->ntp_server[sizeof(cfg->ntp_server) - 1] = '\0';

    AppConfig_Save();
    LedStatusRefresh();

    ESP_LOGI(TAG, "Wifi config saved (ntp=%s, led_normal_on=%u, led_brightness=%u)",
             cfg->ntp_server, cfg->led_normal_on, cfg->led_brightness);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t ApiWifiScanHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting WiFi scan...");

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, true);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
        return ESP_FAIL;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Found %d APs", ap_count);

    if (ap_count == 0)
    {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "[]", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    if (ap_count > 20) ap_count = 20;

    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (ap_list == NULL)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }

    uint16_t ap_num = ap_count;
    esp_wifi_scan_get_ap_records(&ap_num, ap_list);

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < ap_num; i++)
    {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (char *)ap_list[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", ap_list[i].rssi);
        cJSON_AddNumberToObject(item, "authmode", ap_list[i].authmode);
        cJSON_AddItemToArray(root, item);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(ap_list);

    if (json_str == NULL)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);

    ESP_LOGI(TAG, "Scan result sent");
    return ESP_OK;
}

esp_err_t BspWebWifiRegister(httpd_handle_t server)
{
    httpd_uri_t api_wifi_get = {
        .uri      = "/api/wifi_config",
        .method   = HTTP_GET,
        .handler  = ApiWifiConfigGetHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_wifi_get);

    httpd_uri_t api_wifi_post = {
        .uri      = "/api/wifi_config",
        .method   = HTTP_POST,
        .handler  = ApiWifiConfigPostHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_wifi_post);

    httpd_uri_t api_wifi_scan = {
        .uri      = "/api/wifi_scan",
        .method   = HTTP_GET,
        .handler  = ApiWifiScanHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_wifi_scan);

    return ESP_OK;
}