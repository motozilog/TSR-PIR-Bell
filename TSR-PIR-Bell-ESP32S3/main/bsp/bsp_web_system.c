#include "bsp_webserver.h"
#include "bsp_player.h"
#include "bsp_es8311.h"
#include "app_config.h"
#include "app_chirp.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "BSP_WEB_SYSTEM";

/**
 * @函数说明        POST /api/reboot
 */
static esp_err_t ApiRebootHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "========== /api/reboot called ==========");

    const char *resp = "Rebooting...";
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(500));

    esp_sleep_enable_timer_wakeup(100 * 1000);
    esp_deep_sleep_start();

    return ESP_OK;
}

/**
 * @函数说明        把 __DATE__（"Mmm dd yyyy"，如 "Nov 14 2026"，日可能带前导空格）
 *                  转成 "yyyy-MM-dd"（如 "2026-11-14"）
 * @传入参数        date_str  __DATE__ 字符串
 * @传入参数        out       输出缓冲
 * @传入参数        out_len   缓冲长度（≥11）
 * @函数返回        true=成功  false=解析失败
 */
static bool ConvertBuildDate(const char *date_str, char *out, size_t out_len)
{
    if (date_str == NULL || out == NULL || out_len < 11) return false;

    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    char mon[4] = {0};
    int day = 0, year = 0;

    /* __DATE__ 固定格式："Mmm dd yyyy"，日可能 " 4" 或 "14" */
    if (sscanf(date_str, "%3s %d %d", mon, &day, &year) != 3) {
        return false;
    }

    int mon_idx = -1;
    for (int i = 0; i < 12; i++) {
        if (strcmp(mon, months[i]) == 0) { mon_idx = i + 1; break; }
    }
    if (mon_idx < 0 || day < 1 || day > 31 || year < 2000) {
        return false;
    }

    snprintf(out, out_len, "%04d-%02d-%02d", year, mon_idx, day);
    return true;
}

/**
 * @函数说明        GET /api/device_info
 *                  需鉴权，返回 MAC + 固件版本 + 编译日期 + 芯片
 */
static esp_err_t ApiDeviceInfoHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    /* 取 MAC（优先 STA，AP 兜底） */
    uint8_t mac[6] = {0};
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (ret != ESP_OK) {
        ret = esp_wifi_get_mac(WIFI_IF_AP, mac);
    }

    char mac_str[32];
    snprintf(mac_str, sizeof(mac_str), MACSTR, MAC2STR(mac));

    /* 编译日期转换：__DATE__ "Nov 14 2026" -> "2026-11-14" */
    char build_date[16] = {0};
    if (!ConvertBuildDate(__DATE__, build_date, sizeof(build_date))) {
        ESP_LOGW(TAG, "Convert build date failed: %s", __DATE__);
        build_date[0] = '\0';
    }

    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "mac", mac_str);
    cJSON_AddStringToObject(out, "version", FW_VERSION);
    cJSON_AddStringToObject(out, "build_date", build_date);
    cJSON_AddStringToObject(out, "chip", "ESP32S3");

    char *out_str = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);

    ESP_LOGI(TAG, "device info: %s", out_str);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out_str, HTTPD_RESP_USE_STRLEN);
    free(out_str);
    return ESP_OK;
}

/**
 * @函数说明        GET /api/volume
 */
static esp_err_t ApiVolumeGetHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    SysConfig_t *cfg = AppConfig_Get();
    uint8_t vol = cfg->volume ? cfg->volume : 70;
    if (vol < 10) vol = 10;
    if (vol > 100) vol = 100;

    char buf[32];
    snprintf(buf, sizeof(buf), "{\"volume\":%u}", vol);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* 音量试听：播完后恢复原音量（延时 6 秒，覆盖常见 default.mp3） */
static void VolumeRestoreTask(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(6000));

    SysConfig_t *c = AppConfig_Get();
    uint8_t ov = c->volume ? c->volume : 70;
    if (ov < 10) ov = 10;
    if (ov > 100) ov = 100;
    if (Es8311IsReady()) {
        Es8311SetVolume(ov);
    }
    vTaskDelete(NULL);
}

/**
 * @函数说明        POST /api/volume_test
 *                  body {"volume":70}
 *                  仅临时试听：设置音量 → 播放 default.mp3 → 播完恢复原音量
 *                  不写配置、不落盘
 */
static esp_err_t ApiVolumeTestHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    char buf[64] = {0};

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

    cJSON *vol_item = cJSON_GetObjectItem(root, "volume");
    if (!cJSON_IsNumber(vol_item))
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing volume");
        return ESP_FAIL;
    }

    int vol = vol_item->valueint;
    if (vol < 10) vol = 10;
    if (vol > 100) vol = 100;
    cJSON_Delete(root);

    SysConfig_t *cfg = AppConfig_Get();
    uint8_t orig_vol = cfg->volume ? cfg->volume : 70;
    if (orig_vol < 10) orig_vol = 10;
    if (orig_vol > 100) orig_vol = 100;

    if (Es8311IsReady()) {
        Es8311SetVolume((uint8_t)vol);
    }

    PlayerTryPlay(PLAYER_DEFAULT_FILE, PLAYER_OWNER_WEB, 0, 0);

    xTaskCreate(VolumeRestoreTask, "vol_restore", 2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "volume test: vol=%d (temp, will restore to %u)", vol, orig_vol);

    char resp[80];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"volume\":%d,\"saved\":false}", vol);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @函数说明        POST /api/test_chirp
 *                  Body: {"hex":"00015A0CE4"}
 *                  鉴权：统一走 Cookie 会话
 */
static esp_err_t ApiTestChirpHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    char buf[128] = {0};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *hex_item = cJSON_GetObjectItem(root, "hex");
    if (!cJSON_IsString(hex_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing hex");
        return ESP_FAIL;
    }

    const char *hex = hex_item->valuestring;
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0 || hex_len == 0 || hex_len > APP_CHIRP_DATA_MAX * 2) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid hex length");
        return ESP_FAIL;
    }

    uint8_t data[APP_CHIRP_DATA_MAX] = {0};
    uint8_t data_len = (uint8_t)(hex_len / 2);
    for (int i = 0; i < data_len; i++) {
        unsigned int val = 0;
        if (sscanf(&hex[i * 2], "%2x", &val) != 1) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid hex format");
            return ESP_FAIL;
        }
        data[i] = (uint8_t)val;
    }
    cJSON_Delete(root);

    ESP_LOGI(TAG, "test_chirp: inject %u bytes", data_len);
    AppChirp_OnFrame(data, data_len);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* 校验密码合法性 */
static bool IsValidPassword(const char *pw)
{
    if (pw == NULL) return false;
    size_t len = strlen(pw);
    if (len < 1 || len > 16) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)pw[i];
        if (c < 0x21 || c > 0x7E) return false;
    }
    return true;
}

/**
 * @函数说明        POST /api/password
 */
static esp_err_t ApiPasswordPostHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    char buf[128] = {0};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *pw_item = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(pw_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing password");
        return ESP_FAIL;
    }

    const char *newpw = pw_item->valuestring;
    if (!IsValidPassword(newpw)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "password rejected: invalid format");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req,
            "{\"ok\":false,\"msg\":\"密码需为1~16位英文/数字/符号，不含空格\"}",
            HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    SysConfig_t *cfg = AppConfig_Get();
    strncpy(cfg->login_password, newpw, sizeof(cfg->login_password) - 1);
    cfg->login_password[sizeof(cfg->login_password) - 1] = '\0';
    cJSON_Delete(root);

    AppConfig_Save();
    ESP_LOGI(TAG, "login password updated (len=%u)", (unsigned)strlen(cfg->login_password));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t BspWebSystemRegister(httpd_handle_t server)
{
    httpd_uri_t api_reboot = {
        .uri      = "/api/reboot",
        .method   = HTTP_POST,
        .handler  = ApiRebootHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_reboot);

    httpd_uri_t api_device_info = {
        .uri      = "/api/device_info",
        .method   = HTTP_GET,
        .handler  = ApiDeviceInfoHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_device_info);

    httpd_uri_t api_volume_get = {
        .uri      = "/api/volume",
        .method   = HTTP_GET,
        .handler  = ApiVolumeGetHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_volume_get);

    httpd_uri_t api_volume_test = {
        .uri      = "/api/volume_test",
        .method   = HTTP_POST,
        .handler  = ApiVolumeTestHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_volume_test);

    httpd_uri_t api_test_chirp = {
        .uri      = "/api/test_chirp",
        .method   = HTTP_POST,
        .handler  = ApiTestChirpHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_test_chirp);

    httpd_uri_t api_password_post = {
        .uri      = "/api/password",
        .method   = HTTP_POST,
        .handler  = ApiPasswordPostHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_password_post);

    return ESP_OK;
}