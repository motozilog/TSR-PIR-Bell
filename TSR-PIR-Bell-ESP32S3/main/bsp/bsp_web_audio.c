#include "bsp_webserver.h"
#include "bsp_littlefs.h"
#include "bsp_player.h"
#include "app_config.h"

#include "esp_log.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static const char *TAG = "BSP_WEB_AUDIO";

/**
 * @函数说明        POST /api/play?file=xxx.mp3
 */
static esp_err_t ApiPlayHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    char query[128];
    char filename[PLAYER_FILENAME_MAX] = {0};
    char full_path[PLAYER_FILENAME_MAX + 2];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
    {
        if (httpd_query_key_value(query, "file", filename, sizeof(filename)) == ESP_OK)
        {
            ESP_LOGI(TAG, "/api/play file=%s", filename);
        }
    }

    if (filename[0] == '\0')
    {
        ESP_LOGI(TAG, "/api/play no file, use default");
        PlayerTryPlay(NULL, PLAYER_OWNER_WEB, 0, 0);
    }
    else
    {
        snprintf(full_path, sizeof(full_path), "/%s", filename);
        PlayerTryPlay(full_path, PLAYER_OWNER_WEB, 0, 0);
    }

    const char *resp = "OK";
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * POST /api/upload_pcm?name=audioXX.mp3
 * 支持 audio01~10 / lowpwr01~04 / offline01~04 / fullpwr01~04
 */
static esp_err_t ApiUploadPcmHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    char query[128];
    char name[64] = {0};
    char filepath[128];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
    {
        httpd_query_key_value(query, "name", name, sizeof(name));
    }

    if (name[0] == '\0')
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing name");
        return ESP_FAIL;
    }

    #define MAX_AUDIO_SIZE  (200 * 1024)
    if (req->content_len <= 0 || req->content_len > MAX_AUDIO_SIZE)
    {
        ESP_LOGW(TAG, "Upload too large: %d bytes", req->content_len);
        char r[96];
        snprintf(r, sizeof(r),
                 "{\"ok\":false,\"msg\":\"文件大小超出限制(最大200KB)\"}");
        httpd_resp_set_status(req, "413 Content Too Large");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, r, HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    bool name_ok = false;
    for (int i = 1; i <= 10; i++) {
        char expected[16];
        snprintf(expected, sizeof(expected), "audio%02d.mp3", i);
        if (strcmp(name, expected) == 0) { name_ok = true; break; }
    }
    if (!name_ok) {
        for (int i = 1; i <= 4; i++) {
            char expected[16];
            snprintf(expected, sizeof(expected), "lowpwr%02d.mp3", i);
            if (strcmp(name, expected) == 0) { name_ok = true; break; }
            snprintf(expected, sizeof(expected), "offline%02d.mp3", i);
            if (strcmp(name, expected) == 0) { name_ok = true; break; }
            snprintf(expected, sizeof(expected), "fullpwr%02d.mp3", i);
            if (strcmp(name, expected) == 0) { name_ok = true; break; }
        }
    }
    if (!name_ok) {
        ESP_LOGW(TAG, "Upload rejected: %s", name);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Only audio01~10 / lowpwr01~04 / offline01~04 / fullpwr01~04 allowed");
        return ESP_FAIL;
    }

    snprintf(filepath, sizeof(filepath), "%s/%s", LITTLEFS_MOUNT_POINT, name);
    ESP_LOGI(TAG, "Upload audio: %s (content_len=%d)", filepath, req->content_len);

    FILE *fd = fopen(filepath, "wb");
    if (fd == NULL)
    {
        ESP_LOGE(TAG, "Failed to open %s for write", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File open failed");
        return ESP_FAIL;
    }

    char buf[4096];
    int total = 0;
    int remaining = req->content_len;
    while (remaining > 0)
    {
        int received = httpd_req_recv(req, buf,
                                      sizeof(buf) < remaining ? sizeof(buf) : remaining);
        if (received <= 0)
        {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "Recv failed: %d", received);
            fclose(fd);
            remove(filepath);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv failed");
            return ESP_FAIL;
        }
        fwrite(buf, 1, received, fd);
        total += received;
        remaining -= received;
    }
    fclose(fd);

    ESP_LOGI(TAG, "Upload done: %s (%d bytes), verify play", filepath, total);

    char play_path[PLAYER_FILENAME_MAX + 2];
    snprintf(play_path, sizeof(play_path), "/%s", name);
    bool played_ok = (PlayerPlayFileSync(play_path) == ESP_OK);

    if (!played_ok) {
        remove(filepath);
        ESP_LOGW(TAG, "MP3 verify failed, file removed: %s", filepath);
        char r[96];
        snprintf(r, sizeof(r),
                 "{\"ok\":false,\"msg\":\"MP3 格式校验或播放失败，已丢弃\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, r, HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "MP3 verified and kept: %s", filepath);
    char r[96];
    snprintf(r, sizeof(r), "{\"ok\":true,\"msg\":\"上传成功，已播放校验\"}");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, r, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * POST /api/upload_default_mp3
 */
static esp_err_t ApiUploadDefaultMp3Handler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    #define MAX_MP3_SIZE   (200 * 1024)
    const char *temp_path = LITTLEFS_MOUNT_POINT "/temp.mp3";
    const char *dst_path  = LITTLEFS_MOUNT_POINT "/default.mp3";

    char resp[128];
    bool played_ok = false;
    esp_err_t result = ESP_FAIL;

    if (req->content_len <= 0 || req->content_len > MAX_MP3_SIZE) {
        ESP_LOGW(TAG, "default mp3 size invalid: %d", req->content_len);
        snprintf(resp, sizeof(resp),
                 "{\"ok\":false,\"played\":false,\"msg\":\"文件大小超出限制(最大200KB)\"}");
        goto send_resp;
    }

    FILE *fd = fopen(temp_path, "wb");
    if (fd == NULL) {
        ESP_LOGE(TAG, "open %s failed", temp_path);
        snprintf(resp, sizeof(resp),
                 "{\"ok\":false,\"played\":false,\"msg\":\"无法创建临时文件\"}");
        goto send_resp;
    }

    char buf[4096];
    int remaining = req->content_len;
    int total = 0;
    while (remaining > 0) {
        int got = httpd_req_recv(req, buf, sizeof(buf) < remaining ? sizeof(buf) : remaining);
        if (got <= 0) {
            if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
            fclose(fd);
            remove(temp_path);
            ESP_LOGE(TAG, "recv failed: %d", got);
            snprintf(resp, sizeof(resp),
                     "{\"ok\":false,\"played\":false,\"msg\":\"接收失败\"}");
            goto send_resp;
        }
        fwrite(buf, 1, got, fd);
        total += got;
        remaining -= got;
    }
    fclose(fd);
    ESP_LOGI(TAG, "temp.mp3 saved (%d bytes), start verify play", total);

    played_ok = (PlayerPlayFileSync("/temp.mp3") == ESP_OK);

    if (!played_ok) {
        remove(temp_path);
        snprintf(resp, sizeof(resp),
                 "{\"ok\":false,\"played\":false,\"msg\":\"MP3 格式校验或播放失败，已丢弃\"}");
        goto send_resp;
    }

    remove(dst_path);
    if (rename(temp_path, dst_path) != 0) {
        ESP_LOGE(TAG, "rename temp -> default failed");
        remove(temp_path);
        snprintf(resp, sizeof(resp),
                 "{\"ok\":false,\"played\":true,\"msg\":\"播放成功但保存失败\"}");
        goto send_resp;
    }

    ESP_LOGI(TAG, "default.mp3 replaced by upload (%d bytes)", total);
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"played\":true,\"msg\":\"播放成功，已设为开机音频\"}");
    result = ESP_OK;

send_resp:
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return result == ESP_OK ? ESP_OK : ESP_FAIL;
}

/**
 * GET /api/audio_list
 */
static esp_err_t ApiAudioListHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();

    char path[96];
    struct stat st;
    for (int i = 1; i <= 10; i++) {
        snprintf(path, sizeof(path), "%s/audio%02d.mp3", LITTLEFS_MOUNT_POINT, i);
        int exists = (stat(path, &st) == 0 && S_ISREG(st.st_mode)) ? 1 : 0;
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(exists));
    }
    cJSON_AddItemToObject(root, "exists", arr);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    free(out);
    return ESP_OK;
}

/**
 * GET /api/alert_audio_list
 * 返回 lowpwr/offline/fullpwr 各 4 个文件存在状态
 */
static esp_err_t ApiAlertAudioListHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *lp = cJSON_CreateArray();
    cJSON *of = cJSON_CreateArray();
    cJSON *fp = cJSON_CreateArray();

    char path[96];
    struct stat st;
    for (int i = 1; i <= 4; i++) {
        snprintf(path, sizeof(path), "%s/lowpwr%02d.mp3", LITTLEFS_MOUNT_POINT, i);
        cJSON_AddItemToArray(lp, cJSON_CreateNumber(
            (stat(path, &st) == 0 && S_ISREG(st.st_mode)) ? 1 : 0));

        snprintf(path, sizeof(path), "%s/offline%02d.mp3", LITTLEFS_MOUNT_POINT, i);
        cJSON_AddItemToArray(of, cJSON_CreateNumber(
            (stat(path, &st) == 0 && S_ISREG(st.st_mode)) ? 1 : 0));

        snprintf(path, sizeof(path), "%s/fullpwr%02d.mp3", LITTLEFS_MOUNT_POINT, i);
        cJSON_AddItemToArray(fp, cJSON_CreateNumber(
            (stat(path, &st) == 0 && S_ISREG(st.st_mode)) ? 1 : 0));
    }
    cJSON_AddItemToObject(root, "lowpwr",  lp);
    cJSON_AddItemToObject(root, "offline", of);
    cJSON_AddItemToObject(root, "fullpwr", fp);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    free(out);
    return ESP_OK;
}

/**
 * @函数说明        GET /api/audio_config
 */
static esp_err_t ApiAudioConfigGetHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    SysConfig_t *cfg = AppConfig_Get();

    cJSON *root = cJSON_CreateObject();
    cJSON *notes = cJSON_CreateObject();

    for (int i = 0; i < 10; i++)
    {
        if (cfg->audio_notes[i][0] != '\0')
        {
            char key[4];
            snprintf(key, sizeof(key), "%d", i + 1);
            cJSON_AddStringToObject(notes, key, cfg->audio_notes[i]);
        }
    }
    cJSON_AddItemToObject(root, "notes", notes);

    char *out_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out_str, HTTPD_RESP_USE_STRLEN);
    free(out_str);
    return ESP_OK;
}

/**
 * @函数说明        POST /api/audio_config
 */
static esp_err_t ApiAudioConfigPostHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    char buf[1024] = {0};

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

    cJSON *notes = cJSON_GetObjectItem(root, "notes");
    if (cJSON_IsObject(notes))
    {
        for (int i = 0; i < 10; i++) cfg->audio_notes[i][0] = '\0';

        for (int i = 0; i < 10; i++)
        {
            char key[4];
            snprintf(key, sizeof(key), "%d", i + 1);
            cJSON *v = cJSON_GetObjectItem(notes, key);
            if (cJSON_IsString(v))
            {
                strncpy(cfg->audio_notes[i], v->valuestring, sizeof(cfg->audio_notes[i]) - 1);
            }
        }
    }

    cJSON_Delete(root);

    AppConfig_Save();

    ESP_LOGI(TAG, "Audio config saved");

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t ApiBootAudioGetHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    SysConfig_t *cfg = AppConfig_Get();
    char buf[32];
    snprintf(buf, sizeof(buf), "{\"play\":%d}", cfg->boot_play ? 1 : 0);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t ApiBootAudioPostHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    char buf[64] = {0};
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

    SysConfig_t *cfg = AppConfig_Get();
    cJSON *play = cJSON_GetObjectItem(root, "play");
    if (cJSON_IsNumber(play)) {
        cfg->boot_play = (play->valueint != 0) ? 1 : 0;
        AppConfig_Save();
    }
    cJSON_Delete(root);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t BspWebAudioRegister(httpd_handle_t server)
{
    httpd_uri_t api_play = {
        .uri      = "/api/play",
        .method   = HTTP_POST,
        .handler  = ApiPlayHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_play);

    httpd_uri_t api_upload_pcm = {
        .uri      = "/api/upload_pcm",
        .method   = HTTP_POST,
        .handler  = ApiUploadPcmHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_upload_pcm);

    httpd_uri_t api_upload_default_mp3 = {
        .uri      = "/api/upload_default_mp3",
        .method   = HTTP_POST,
        .handler  = ApiUploadDefaultMp3Handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_upload_default_mp3);

    httpd_uri_t api_audio_list = {
        .uri      = "/api/audio_list",
        .method   = HTTP_GET,
        .handler  = ApiAudioListHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_audio_list);

    httpd_uri_t api_alert_audio_list = {
        .uri      = "/api/alert_audio_list",
        .method   = HTTP_GET,
        .handler  = ApiAlertAudioListHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_alert_audio_list);

    httpd_uri_t api_audio_get = {
        .uri      = "/api/audio_config",
        .method   = HTTP_GET,
        .handler  = ApiAudioConfigGetHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_audio_get);

    httpd_uri_t api_audio_post = {
        .uri      = "/api/audio_config",
        .method   = HTTP_POST,
        .handler  = ApiAudioConfigPostHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_audio_post);

    httpd_uri_t api_boot_audio_get = {
        .uri      = "/api/boot_audio",
        .method   = HTTP_GET,
        .handler  = ApiBootAudioGetHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_boot_audio_get);

    httpd_uri_t api_boot_audio_post = {
        .uri      = "/api/boot_audio",
        .method   = HTTP_POST,
        .handler  = ApiBootAudioPostHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_boot_audio_post);

    return ESP_OK;
}