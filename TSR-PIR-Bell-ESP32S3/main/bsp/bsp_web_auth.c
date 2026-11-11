#include "bsp_webserver.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "BSP_WEB_AUTH";

/* 默认登录密码（未配置时兜底） */
#define DEFAULT_LOGIN_PASSWORD  "admin"

/* token 有效期：48 小时（滑动刷新） */
#define TOKEN_TTL_US            (48LL * 3600LL * 1000000LL)

/* 最大并发会话数 */
#define MAX_SESSIONS            4

/* 单个会话 */
typedef struct {
    char    token[33];      /* 32 hex + '\0'，全 '\0' 表示空槽 */
    int64_t expire_us;      /* 过期时刻，0 表示空槽 */
} Session_t;

static Session_t s_sessions[MAX_SESSIONS];

/* ===== 取当前登录密码（空则返回默认） ===== */
static const char* GetLoginPassword(void)
{
    SysConfig_t *cfg = AppConfig_Get();
    if (cfg->login_password[0] != '\0') {
        return cfg->login_password;
    }
    return DEFAULT_LOGIN_PASSWORD;
}

/* ===== 随机 token 生成（16 字节 -> 32 hex） ===== */
static void GenerateToken(char *out, size_t out_len)
{
    uint8_t raw[16];
    esp_fill_random(raw, sizeof(raw));
    for (int i = 0; i < 16; i++) {
        snprintf(out + i * 2, 3, "%02x", raw[i]);
    }
    out[32] = '\0';
    (void)out_len;
}

/* ===== 从 Cookie header 中取 tsr_token ===== */
static bool ParseTokenCookie(httpd_req_t *req, char *out, size_t out_len)
{
    if (out == NULL || out_len < 33) return false;

    char cookie[256] = {0};
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK) {
        return false;
    }

    const char *key = "tsr_token=";
    const char *p = strstr(cookie, key);
    if (p == NULL) return false;
    p += strlen(key);

    size_t i = 0;
    while (p[i] != '\0' && p[i] != ';' && p[i] != ' ' && i < out_len - 1) {
        out[i] = p[i];
        i++;
    }
    out[i] = '\0';
    return (i > 0);
}

/* ===== 在会话表中查找 token，返回下标；找不到返回 -1 ===== */
static int FindSession(const char *token)
{
    if (token == NULL || token[0] == '\0') return -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].token[0] != '\0' &&
            strcmp(s_sessions[i].token, token) == 0) {
            return i;
        }
    }
    return -1;
}

/* ===== 清空一个槽 ===== */
static void ClearSession(int idx)
{
    if (idx < 0 || idx >= MAX_SESSIONS) return;
    s_sessions[idx].token[0]  = '\0';
    s_sessions[idx].expire_us = 0;
}

/* ===== 清空所有槽 ===== */
static void ClearAllSessions(void)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        ClearSession(i);
    }
}

/* ===== 找一个可用槽：优先空槽，没有就淘汰最早过期的（LRU） ===== */
static int AllocSessionSlot(void)
{
    /* 1. 找空槽 */
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].token[0] == '\0') {
            return i;
        }
    }

    /* 2. 全满，淘汰 expire_us 最小的（最早过期，即最久没活动） */
    int victim = 0;
    for (int i = 1; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].expire_us < s_sessions[victim].expire_us) {
            victim = i;
        }
    }
    ESP_LOGW(TAG, "session table full, evict slot %d", victim);
    return victim;
}

/* ===== 统一鉴权校验 ===== */
bool BspWebCheckAuth(httpd_req_t *req)
{
    int64_t now = esp_timer_get_time();

    /* 先做一次过期清理，避免过期槽一直被占 */
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].token[0] != '\0' && now > s_sessions[i].expire_us) {
            ClearSession(i);
        }
    }

    char tok[33] = {0};
    if (!ParseTokenCookie(req, tok, sizeof(tok))) {
        return false;
    }

    int idx = FindSession(tok);
    if (idx < 0) {
        return false;
    }

    /* 滑动刷新过期时间 */
    s_sessions[idx].expire_us = now + TOKEN_TTL_US;
    return true;
}

/* ===== POST /api/login ===== */
static esp_err_t ApiLoginHandler(httpd_req_t *req)
{
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

    cJSON *pass_item = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(pass_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing password");
        return ESP_FAIL;
    }

    const char *expected = GetLoginPassword();
    if (strcmp(pass_item->valuestring, expected) != 0) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "login failed: wrong password");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"msg\":\"密码错误\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    cJSON_Delete(root);

    /* 分配槽位（空槽 / LRU 淘汰） */
    int slot = AllocSessionSlot();
    if (slot < 0) {
        /* 理论不会发生，兜底 */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No session slot");
        return ESP_FAIL;
    }

    /* 若该槽已有旧 token，先清 */
    ClearSession(slot);

    /* 生成新 token */
    GenerateToken(s_sessions[slot].token, sizeof(s_sessions[slot].token));
    s_sessions[slot].expire_us = esp_timer_get_time() + TOKEN_TTL_US;

    ESP_LOGI(TAG, "login OK, token issued (slot=%d)", slot);

    /* Set-Cookie: tsr_token=xxx; Path=/; Max-Age=172800; HttpOnly; SameSite=Strict */
    char cookie[128];
    snprintf(cookie, sizeof(cookie),
             "tsr_token=%s; Path=/; Max-Age=172800; HttpOnly; SameSite=Strict",
             s_sessions[slot].token);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ===== POST /api/logout ===== */
static esp_err_t ApiLogoutHandler(httpd_req_t *req)
{
    /* 只清当前请求 Cookie 对应的会话，不动其他浏览器 */
    char tok[33] = {0};
    if (ParseTokenCookie(req, tok, sizeof(tok))) {
        int idx = FindSession(tok);
        if (idx >= 0) {
            ClearSession(idx);
            ESP_LOGI(TAG, "logout: slot %d cleared", idx);
        }
    }

    /* 让浏览器删 Cookie */
    httpd_resp_set_hdr(req, "Set-Cookie",
        "tsr_token=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ===== GET /api/auth_check ===== */
static esp_err_t ApiAuthCheckHandler(httpd_req_t *req)
{
    if (!BspWebCheckAuth(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ===== 注册 ===== */
esp_err_t BspWebLoginRegister(httpd_handle_t server)
{
    /* 初始化会话表 */
    ClearAllSessions();

    httpd_uri_t api_login = {
        .uri      = "/api/login",
        .method   = HTTP_POST,
        .handler  = ApiLoginHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_login);

    httpd_uri_t api_logout = {
        .uri      = "/api/logout",
        .method   = HTTP_POST,
        .handler  = ApiLogoutHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_logout);

    httpd_uri_t api_auth_check = {
        .uri      = "/api/auth_check",
        .method   = HTTP_GET,
        .handler  = ApiAuthCheckHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_auth_check);

    return ESP_OK;
}