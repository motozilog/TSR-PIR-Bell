#include "bsp_webserver.h"
#include "app_config.h"
#include "app_chirp.h"

#include "esp_log.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "BSP_WEB_DOORBELL";

/**
 * @函数说明        GET /api/doorbell_config
 */
static esp_err_t ApiDoorbellConfigGetHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    SysConfig_t *cfg = AppConfig_Get();

    cJSON *root = cJSON_CreateObject();
    cJSON *doorbells = cJSON_CreateObject();

    for (int i = 0; i < 4; i++)
    {
        char key[4];
        snprintf(key, sizeof(key), "%d", i + 1);

        cJSON *db = cJSON_CreateObject();
        const char *mode_str = "welcome";
        if (cfg->doorbells[i].mode == 1) mode_str = "doorbell";
        else if (cfg->doorbells[i].mode == 2) mode_str = "alarm";

        cJSON_AddNumberToObject(db, "enabled", cfg->doorbells[i].enabled ? 1 : 0);
        cJSON_AddStringToObject(db, "mode", mode_str);
        cJSON_AddNumberToObject(db, "lowpwr_enabled",  cfg->doorbells[i].lowpwr_enabled  ? 1 : 0);
        cJSON_AddNumberToObject(db, "offline_enabled", cfg->doorbells[i].offline_enabled ? 1 : 0);
        cJSON_AddNumberToObject(db, "fullpwr_enabled", cfg->doorbells[i].fullpwr_enabled ? 1 : 0);
        cJSON_AddNumberToObject(db, "welcome_ir",  cfg->doorbells[i].welcome_ir_audio);
        cJSON_AddNumberToObject(db, "welcome_key", cfg->doorbells[i].welcome_key_audio);
        cJSON_AddNumberToObject(db, "doorbell_ir", cfg->doorbells[i].doorbell_ir_audio);
        cJSON_AddNumberToObject(db, "doorbell_key",cfg->doorbells[i].doorbell_key_audio);
        cJSON_AddNumberToObject(db, "alarm_ir",    cfg->doorbells[i].alarm_ir_audio);
        cJSON_AddNumberToObject(db, "alarm_key",   cfg->doorbells[i].alarm_key_audio);

        cJSON_AddItemToObject(doorbells, key, db);
    }
    cJSON_AddItemToObject(root, "doorbells", doorbells);

    char *out_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out_str, HTTPD_RESP_USE_STRLEN);
    free(out_str);
    return ESP_OK;
}

/**
 * @函数说明        POST /api/doorbell_config
 */
static esp_err_t ApiDoorbellConfigPostHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    char buf[2048] = {0};

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

    cJSON *doorbells = cJSON_GetObjectItem(root, "doorbells");
    if (cJSON_IsObject(doorbells))
    {
        for (int i = 0; i < 4; i++)
        {
            char key[4];
            snprintf(key, sizeof(key), "%d", i + 1);
            cJSON *db = cJSON_GetObjectItem(doorbells, key);
            if (!cJSON_IsObject(db)) continue;

            cJSON *v;
            if (cJSON_IsNumber(v = cJSON_GetObjectItem(db, "enabled")))
                cfg->doorbells[i].enabled = (v->valueint != 0) ? 1 : 0;

            if (cJSON_IsString(v = cJSON_GetObjectItem(db, "mode")))
            {
                if (strcmp(v->valuestring, "doorbell") == 0) cfg->doorbells[i].mode = 1;
                else if (strcmp(v->valuestring, "alarm") == 0) cfg->doorbells[i].mode = 2;
                else cfg->doorbells[i].mode = 0;
            }

            if (cJSON_IsNumber(v = cJSON_GetObjectItem(db, "lowpwr_enabled")))
                cfg->doorbells[i].lowpwr_enabled = (v->valueint != 0) ? 1 : 0;
            if (cJSON_IsNumber(v = cJSON_GetObjectItem(db, "offline_enabled")))
                cfg->doorbells[i].offline_enabled = (v->valueint != 0) ? 1 : 0;
            if (cJSON_IsNumber(v = cJSON_GetObjectItem(db, "fullpwr_enabled")))
                cfg->doorbells[i].fullpwr_enabled = (v->valueint != 0) ? 1 : 0;

            if (cJSON_IsNumber(v = cJSON_GetObjectItem(db, "welcome_ir")))  cfg->doorbells[i].welcome_ir_audio = v->valueint;
            if (cJSON_IsNumber(v = cJSON_GetObjectItem(db, "welcome_key"))) cfg->doorbells[i].welcome_key_audio = v->valueint;
            if (cJSON_IsNumber(v = cJSON_GetObjectItem(db, "doorbell_ir"))) cfg->doorbells[i].doorbell_ir_audio = v->valueint;
            if (cJSON_IsNumber(v = cJSON_GetObjectItem(db, "doorbell_key")))cfg->doorbells[i].doorbell_key_audio = v->valueint;
            if (cJSON_IsNumber(v = cJSON_GetObjectItem(db, "alarm_ir")))    cfg->doorbells[i].alarm_ir_audio = v->valueint;
            if (cJSON_IsNumber(v = cJSON_GetObjectItem(db, "alarm_key")))   cfg->doorbells[i].alarm_key_audio = v->valueint;
        }
    }

    cJSON_Delete(root);

    AppConfig_Save();

    ESP_LOGI(TAG, "Doorbell config saved");

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @函数说明        GET /api/doorbell_status
 *                  需鉴权，返回完整信息（含电量、last_report）
 */
static esp_err_t ApiDoorbellStatusHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();

    for (uint8_t i = 1; i <= 4; i++) {
        AppChirpDbState_t st = AppChirp_GetDoorbellState(i);
        const char *state_str = "disabled";
        switch (st) {
            case APP_CHIRP_DB_ONLINE:  state_str = "online";      break;
            case APP_CHIRP_DB_OFFLINE: state_str = "offline";     break;
            case APP_CHIRP_DB_LOWBATT: state_str = "low_battery"; break;
            default:                   state_str = "disabled";    break;
        }

        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "idx", i);
        cJSON_AddStringToObject(item, "state", state_str);
        cJSON_AddNumberToObject(item, "battery", AppChirp_GetDoorbellBattery(i));
        cJSON_AddNumberToObject(item, "last_report", AppChirp_GetDoorbellLastReport(i));
        cJSON_AddItemToArray(arr, item);
    }
    cJSON_AddItemToObject(root, "status", arr);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    free(out);
    return ESP_OK;
}

/**
 * @函数说明        GET /api/public_status
 *                  公开接口（登录页用），只返回 idx + state
 *                  不含电量、last_report 等敏感信息
 */
static esp_err_t ApiPublicStatusHandler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();

    for (uint8_t i = 1; i <= 4; i++) {
        AppChirpDbState_t st = AppChirp_GetDoorbellState(i);
        const char *state_str = "disabled";
        switch (st) {
            case APP_CHIRP_DB_ONLINE:  state_str = "online";      break;
            case APP_CHIRP_DB_OFFLINE: state_str = "offline";     break;
            case APP_CHIRP_DB_LOWBATT: state_str = "low_battery"; break;
            default:                   state_str = "disabled";    break;
        }

        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "idx", i);
        cJSON_AddStringToObject(item, "state", state_str);
        cJSON_AddItemToArray(arr, item);
    }
    cJSON_AddItemToObject(root, "status", arr);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    free(out);
    return ESP_OK;
}

/**
 * @函数说明        POST /api/doorbell_query_battery?idx=N
 */
static esp_err_t ApiDoorbellQueryBatteryHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    char query[64];
    char idx_str[8] = {0};
    int idx = 0;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "idx", idx_str, sizeof(idx_str)) == ESP_OK) {
            idx = atoi(idx_str);
        }
    }

    if (idx < 1 || idx > 4) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "idx must be 1~4");
        return ESP_FAIL;
    }

    bool ok = AppChirp_QueryBattery((uint8_t)idx);
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":%s,\"idx\":%d}", ok ? "true" : "false", idx);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t BspWebDoorbellRegister(httpd_handle_t server)
{
    httpd_uri_t api_doorbell_get = {
        .uri      = "/api/doorbell_config",
        .method   = HTTP_GET,
        .handler  = ApiDoorbellConfigGetHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_doorbell_get);

    httpd_uri_t api_doorbell_post = {
        .uri      = "/api/doorbell_config",
        .method   = HTTP_POST,
        .handler  = ApiDoorbellConfigPostHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_doorbell_post);

    httpd_uri_t api_doorbell_status = {
        .uri      = "/api/doorbell_status",
        .method   = HTTP_GET,
        .handler  = ApiDoorbellStatusHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_doorbell_status);

    httpd_uri_t api_public_status = {
        .uri      = "/api/public_status",
        .method   = HTTP_GET,
        .handler  = ApiPublicStatusHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_public_status);

    httpd_uri_t api_doorbell_query_battery = {
        .uri      = "/api/doorbell_query_battery",
        .method   = HTTP_POST,
        .handler  = ApiDoorbellQueryBatteryHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_doorbell_query_battery);

    return ESP_OK;
}