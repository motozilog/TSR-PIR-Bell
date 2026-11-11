#include "bsp_webserver.h"
#include "esp_log.h"
#include <string.h>

/* ===== 嵌入文件符号（由 CMake EMBED_FILES 生成） ===== */
extern const uint8_t login_html_start[] asm("_binary_login_html_start");
extern const uint8_t login_html_end[]   asm("_binary_login_html_end");
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");
extern const uint8_t logo_png_start[]   asm("_binary_logo_png_start");
extern const uint8_t logo_png_end[]     asm("_binary_logo_png_end");

extern const uint8_t common_js_start[]   asm("_binary_common_js_start");
extern const uint8_t common_js_end[]     asm("_binary_common_js_end");
extern const uint8_t doorbell_js_start[] asm("_binary_doorbell_js_start");
extern const uint8_t doorbell_js_end[]   asm("_binary_doorbell_js_end");
extern const uint8_t audio_js_start[]    asm("_binary_audio_js_start");
extern const uint8_t audio_js_end[]      asm("_binary_audio_js_end");
extern const uint8_t wifi_js_start[]     asm("_binary_wifi_js_start");
extern const uint8_t wifi_js_end[]       asm("_binary_wifi_js_end");
extern const uint8_t web_main_js_start[] asm("_binary_web_main_js_start");
extern const uint8_t web_main_js_end[]   asm("_binary_web_main_js_end");

typedef struct {
    const char    *uri;
    const uint8_t *start;
    const uint8_t *end;
} EmbeddedFile_t;

static const EmbeddedFile_t s_files[] = {
    {"/index.html",  index_html_start,  index_html_end},
    {"/login.html",  login_html_start,  login_html_end},
    {"/style.css",   style_css_start,   style_css_end},
    {"/logo.png",    logo_png_start,    logo_png_end},
    {"/common.js",   common_js_start,   common_js_end},
    {"/doorbell.js", doorbell_js_start, doorbell_js_end},
    {"/audio.js",    audio_js_start,    audio_js_end},
    {"/wifi.js",     wifi_js_start,     wifi_js_end},
    {"/web-main.js", web_main_js_start, web_main_js_end},
};

static const char *TAG = "BSP_WEBSERVER";

static httpd_handle_t s_server = NULL;

/**
 * @函数说明        根据文件扩展名返回 MIME 类型
 */
static const char* GetMimeType(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext == NULL) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css")  == 0) return "text/css";
    if (strcmp(ext, ".js")   == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".jpg")  == 0) return "image/jpeg";
    if (strcmp(ext, ".ico")  == 0) return "image/x-icon";
    if (strcmp(ext, ".wav")  == 0) return "audio/wav";
    if (strcmp(ext, ".mp3")  == 0) return "audio/mpeg";

    return "application/octet-stream";
}

/**
 * @函数说明        通用静态文件处理函数
 *                  .js 文件需要登录鉴权，其余（html/css/png）公开
 */
static esp_err_t StaticFileHandler(httpd_req_t *req)
{
    const char *uri = req->uri;

    if (strcmp(uri, "/") == 0)
    {
        uri = "/index.html";
    }

    for (size_t i = 0; i < sizeof(s_files) / sizeof(s_files[0]); i++)
    {
        if (strcmp(uri, s_files[i].uri) == 0)
        {
            /* .js 需要鉴权 */
            const char *ext = strrchr(uri, '.');
            if (ext != NULL && strcmp(ext, ".js") == 0) {
                if (!BspWebCheckAuth(req)) {
                    ESP_LOGW(TAG, "GET %s denied: no auth", uri);
                    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
                    return ESP_FAIL;
                }
            }

            size_t len = s_files[i].end - s_files[i].start;

            httpd_resp_set_type(req, GetMimeType(s_files[i].uri));
            httpd_resp_send(req, (const char *)s_files[i].start, len);

            ESP_LOGI(TAG, "GET %s -> embedded (%d bytes)", req->uri, (int)len);
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "File not found (embedded): %s", uri);
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    return ESP_FAIL;
}

static void WebServerRegisterHandlers(httpd_handle_t server)
{
    BspWebLoginRegister(server);
    BspWebSystemRegister(server);
    BspWebWifiRegister(server);
    BspWebAudioRegister(server);
    BspWebDoorbellRegister(server);

    httpd_uri_t static_files = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = StaticFileHandler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &static_files);

    ESP_LOGI(TAG, "URI handlers registered");
}

void WebServerInit(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEBSERVER_PORT;
    config.max_uri_handlers = 32;
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server: 0x%x", ret);
        return;
    }

    WebServerRegisterHandlers(s_server);

    ESP_LOGI(TAG, "Web server started on port %d", WEBSERVER_PORT);
}

httpd_handle_t WebServerGetHandle(void)
{
    return s_server;
}