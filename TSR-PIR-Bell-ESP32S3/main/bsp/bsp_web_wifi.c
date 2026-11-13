#include "bsp_webserver.h"
#include "app_config.h"
#include "bsp_led_status.h"
#include "bsp_es8311.h"
#include "bsp_ntp.h"
#include "bsp_wifistation.h"
#include "bsp_wifiap.h"

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

/* 试连超时 */
#define TRY_GOT_IP_TIMEOUT_MS   15000
#define TRY_NTP_TIMEOUT_MS      15000
#define RESTORE_TIMEOUT_MS      10000

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
    cJSON_AddNumberToObject(out, "volume", cfg->volume ? cfg->volume : 70);

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

/* 旧配置快照，用于试连失败恢复 */
typedef struct {
    uint8_t  wifi_mode;
    char     ssid[64];
    char     password[64];
    uint8_t  ip_mode;
    char     ip[16];
    char     netmask[16];
    char     gateway[16];
    char     dns1[16];
    char     dns2[16];
    char     ntp_server[64];
} WifiCfgSnapshot_t;

static void SnapshotFromCfg(WifiCfgSnapshot_t *snap, SysConfig_t *cfg)
{
    snap->wifi_mode = cfg->wifi_mode;
    memcpy(snap->ssid, cfg->ssid, sizeof(snap->ssid));
    memcpy(snap->password, cfg->password, sizeof(snap->password));
    snap->ip_mode = cfg->ip_mode;
    memcpy(snap->ip, cfg->ip, sizeof(snap->ip));
    memcpy(snap->netmask, cfg->netmask, sizeof(snap->netmask));
    memcpy(snap->gateway, cfg->gateway, sizeof(snap->gateway));
    memcpy(snap->dns1, cfg->dns1, sizeof(snap->dns1));
    memcpy(snap->dns2, cfg->dns2, sizeof(snap->dns2));
    memcpy(snap->ntp_server, cfg->ntp_server, sizeof(snap->ntp_server));
}

/* 用快照恢复连接（尽力） */
static void RestoreConnection(const WifiCfgSnapshot_t *snap)
{
    if (snap->wifi_mode == 1 && snap->ssid[0] != '\0') {
        const char *ip_mode = (snap->ip_mode == 1) ? "static" : "dhcp";
        ESP_LOGI(TAG, "Restore station: %s", snap->ssid);
        if (WifiStationStart(snap->ssid, snap->password, ip_mode,
                             snap->ip, snap->netmask, snap->gateway,
                             snap->dns1, snap->dns2) == ESP_OK) {
            WifiStationWaitConnected(RESTORE_TIMEOUT_MS);
        }
    } else {
        ESP_LOGI(TAG, "Restore AP mode");
        WifiApStart();
    }
}

/* 判断网络配置是否变更（不含音量/NTP/亮灯/亮度） */
static bool IsNetworkChanged(SysConfig_t *cfg, cJSON *mode, cJSON *ssid,
                             cJSON *pass, cJSON *ip_mode, cJSON *ip,
                             cJSON *netmask, cJSON *gateway, cJSON *dns1,
                             cJSON *dns2)
{
    const char *new_mode = cJSON_IsString(mode) ? mode->valuestring :
                           (cfg->wifi_mode == 0 ? "ap" : "station");
    const char *new_ssid = cJSON_IsString(ssid) ? ssid->valuestring : cfg->ssid;
    const char *new_ip_mode = cJSON_IsString(ip_mode) ? ip_mode->valuestring :
                              (cfg->ip_mode == 0 ? "dhcp" : "static");

    if (strcmp(new_mode, cfg->wifi_mode == 0 ? "ap" : "station") != 0) return true;
    if (strcmp(new_ssid, cfg->ssid) != 0) return true;
    if (strcmp(new_ip_mode, cfg->ip_mode == 0 ? "dhcp" : "static") != 0) return true;

    if (strcmp(new_ip_mode, "static") == 0) {
        const char *v;
        v = cJSON_IsString(ip) ? ip->valuestring : cfg->ip;
        if (strcmp(v, cfg->ip) != 0) return true;
        v = cJSON_IsString(netmask) ? netmask->valuestring : cfg->netmask;
        if (strcmp(v, cfg->netmask) != 0) return true;
        v = cJSON_IsString(gateway) ? gateway->valuestring : cfg->gateway;
        if (strcmp(v, cfg->gateway) != 0) return true;
        v = cJSON_IsString(dns1) ? dns1->valuestring : cfg->dns1;
        if (strcmp(v, cfg->dns1) != 0) return true;
        v = cJSON_IsString(dns2) ? dns2->valuestring : cfg->dns2;
        if (strcmp(v, cfg->dns2) != 0) return true;
    }

    /* 密码：用户输入了新值才算变更 */
    if (cJSON_IsString(pass) && pass->valuestring[0] != '\0') return true;

    return false;
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
    cJSON *vol_item = cJSON_GetObjectItem(root, "volume");

    if (!cJSON_IsString(mode))
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mode");
        return ESP_FAIL;
    }

    /* 网络是否变更（决定是否需要试连） */
    bool net_changed = IsNetworkChanged(cfg, mode, ssid, pass, ip_mode,
                                        ip, netmask, gateway, dns1, dns2);

    /* 新 NTP 服务器（若提供） */
    char new_ntp[64];
    cJSON *ntp_item = cJSON_GetObjectItem(root, "ntp_server");
    if (cJSON_IsString(ntp_item) && ntp_item->valuestring[0] != '\0') {
        strncpy(new_ntp, ntp_item->valuestring, sizeof(new_ntp) - 1);
    } else {
        strncpy(new_ntp, cfg->ntp_server[0] ? cfg->ntp_server : "ntp.aliyun.com",
                sizeof(new_ntp) - 1);
    }
    new_ntp[sizeof(new_ntp) - 1] = '\0';

    /* 新配置参数（解析出来，供试连用） */
    const char *new_mode = mode->valuestring;
    char new_ssid[64];
    strncpy(new_ssid, cJSON_IsString(ssid) ? ssid->valuestring : cfg->ssid,
            sizeof(new_ssid) - 1);
    new_ssid[sizeof(new_ssid) - 1] = '\0';

    char new_pass[64];
    if (cJSON_IsString(pass) && pass->valuestring[0] != '\0') {
        strncpy(new_pass, pass->valuestring, sizeof(new_pass) - 1);
    } else {
        strncpy(new_pass, cfg->password, sizeof(new_pass) - 1);
    }
    new_pass[sizeof(new_pass) - 1] = '\0';

    const char *new_ip_mode = cJSON_IsString(ip_mode) ? ip_mode->valuestring :
                              (cfg->ip_mode == 0 ? "dhcp" : "static");
    char new_ip[16], new_netmask[16], new_gateway[16], new_dns1[16], new_dns2[16];
    strncpy(new_ip,      cJSON_IsString(ip)      ? ip->valuestring      : cfg->ip,      sizeof(new_ip) - 1);
    strncpy(new_netmask, cJSON_IsString(netmask) ? netmask->valuestring : cfg->netmask, sizeof(new_netmask) - 1);
    strncpy(new_gateway, cJSON_IsString(gateway) ? gateway->valuestring : cfg->gateway, sizeof(new_gateway) - 1);
    strncpy(new_dns1,    cJSON_IsString(dns1)    ? dns1->valuestring    : cfg->dns1,    sizeof(new_dns1) - 1);
    strncpy(new_dns2,    cJSON_IsString(dns2)    ? dns2->valuestring    : cfg->dns2,    sizeof(new_dns2) - 1);
    new_ip[15] = new_netmask[15] = new_gateway[15] = new_dns1[15] = new_dns2[15] = '\0';

    /* 快照旧配置 */
    WifiCfgSnapshot_t old_snap;
    SnapshotFromCfg(&old_snap, cfg);

    /* ===== 网络变更 + 新配置是 station：先试连 + NTP 校验 ===== */
    if (net_changed && strcmp(new_mode, "station") == 0)
    {
        ESP_LOGI(TAG, "Network changed, trying new station config: %s", new_ssid);

        /* 停当前连接 */
        WifiStationStop();
        vTaskDelay(pdMS_TO_TICKS(200));

        /* 试连新配置 */
        esp_err_t ret = WifiStationStart(new_ssid, new_pass, new_ip_mode,
                                         new_ip, new_netmask, new_gateway,
                                         new_dns1, new_dns2);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WifiStationStart failed: %s", esp_err_to_name(ret));
            RestoreConnection(&old_snap);
            cJSON_Delete(root);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req,
                "{\"ok\":false,\"msg\":\"无法启动WiFi，请检查参数\"}",
                HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        /* 等 GOT_IP */
        bool got_ip = WifiStationWaitConnected(TRY_GOT_IP_TIMEOUT_MS);
        if (!got_ip) {
            ESP_LOGW(TAG, "Try connect failed: no IP in %d ms", TRY_GOT_IP_TIMEOUT_MS);
            WifiStationStop();
            vTaskDelay(pdMS_TO_TICKS(200));
            RestoreConnection(&old_snap);
            cJSON_Delete(root);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req,
                "{\"ok\":false,\"msg\":\"无法获取IP，请检查SSID/密码/网络设置\"}",
                HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        ESP_LOGI(TAG, "Try connect OK, got IP. Testing NTP: %s", new_ntp);

        /* 等 NTP 同步 */
        esp_err_t ntp_ret = NtpSyncOnce(new_ntp, TRY_NTP_TIMEOUT_MS);
        if (ntp_ret != ESP_OK) {
            ESP_LOGW(TAG, "Try NTP failed: %s", esp_err_to_name(ntp_ret));
            WifiStationStop();
            vTaskDelay(pdMS_TO_TICKS(200));
            RestoreConnection(&old_snap);
            cJSON_Delete(root);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req,
                "{\"ok\":false,\"msg\":\"无法连接到NTP服务器，请检查网络或NTP设置\"}",
                HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        ESP_LOGI(TAG, "Try connect + NTP OK, saving config");
        /* 验证通过，继续走保存逻辑（下面统一处理） */
    }

    /* ===== 写 cfg（统一处理，验证已通过或无需验证） ===== */
    cfg->wifi_mode = (strcmp(new_mode, "station") == 0) ? 1 : 0;

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

    uint8_t new_vol = cfg->volume ? cfg->volume : 70;
    if (cJSON_IsNumber(vol_item))
    {
        int v = vol_item->valueint;
        if (v < 10) v = 10;
        if (v > 100) v = 100;
        cfg->volume = (uint8_t)v;
        new_vol = cfg->volume;
    }

    char old_ntp[64];
    strncpy(old_ntp, cfg->ntp_server, sizeof(old_ntp) - 1);
    old_ntp[sizeof(old_ntp) - 1] = '\0';

    strncpy(cfg->ntp_server, new_ntp, sizeof(cfg->ntp_server) - 1);
    cfg->ntp_server[sizeof(cfg->ntp_server) - 1] = '\0';

    cJSON_Delete(root);

    AppConfig_Save();

    /* 音量：立即生效 */
    if (Es8311IsReady()) {
        Es8311SetVolume(new_vol);
    }

    /* NTP：若变更且未试连（即非网络变更场景），运行时切换 */
    if (!net_changed && strcmp(old_ntp, cfg->ntp_server) != 0) {
        ESP_LOGI(TAG, "NTP server changed: %s -> %s", old_ntp, cfg->ntp_server);
        if (NtpSetServer(cfg->ntp_server) != ESP_OK) {
            ESP_LOGW(TAG, "NtpSetServer failed");
        }
    }

    /* LED 亮灯/亮度：立即刷新 */
    LedStatusRefresh();

    ESP_LOGI(TAG, "Wifi config saved (ntp=%s, led_normal_on=%u, led_brightness=%u, volume=%u, net_changed=%d)",
             cfg->ntp_server, cfg->led_normal_on, cfg->led_brightness, cfg->volume, (int)net_changed);

    /* 网络变更 → need_reboot:true（提示重启）；否则即时生效 */
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"need_reboot\":%s}",
             net_changed ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
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