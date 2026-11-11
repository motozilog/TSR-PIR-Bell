#include "app_config.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "APP_CONFIG";

#define CONFIG_FLASH_OFFSET     0x7E0000
#define CONFIG_FLASH_SIZE       (128 * 1024)
#define CONFIG_PART_LABEL       "config"

#define CONFIG_MAGIC            0xCF6A
#define CONFIG_HEADER_SIZE      8
#define CONFIG_JSON_MAX         4000
#define CONFIG_ERASE_SIZE       4096

/* 默认登录密码 */
#define DEFAULT_LOGIN_PASSWORD  "admin"

static const esp_partition_t *s_config_part = NULL;
static SysConfig_t s_config;

static void LoadDefaultValues(SysConfig_t *cfg)
{
    memset(cfg, 0, sizeof(SysConfig_t));

    cfg->local_addr = 0x0001;
    cfg->netid      = 0x00;
    cfg->tx_power   = 0x00;
    cfg->channel    = 0x17;
    cfg->peer_addr  = 0x0000;

    for (int i = 0; i < 16; i++)
    {
        cfg->xtea_key[i] = (i % 2 == 0) ? 0x20 : 0x26;
    }

    cfg->boot_play = 1;
    cfg->volume = 70;
    cfg->led_normal_on = 1;
    cfg->led_brightness = 50;

    /* 默认登录密码 */
    strncpy(cfg->login_password, DEFAULT_LOGIN_PASSWORD,
            sizeof(cfg->login_password) - 1);
    cfg->login_password[sizeof(cfg->login_password) - 1] = '\0';

    cfg->wifi_mode = 0;
    cfg->ip_mode   = 0;

    strncpy(cfg->ntp_server, "ntp.aliyun.com", sizeof(cfg->ntp_server) - 1);

    for (int i = 0; i < 4; i++)
    {
        cfg->doorbells[i].enabled = 0;
        cfg->doorbells[i].mode = 0;
        cfg->doorbells[i].lowpwr_enabled  = 1;
        cfg->doorbells[i].offline_enabled = 1;
        cfg->doorbells[i].fullpwr_enabled = 1;
        cfg->doorbells[i].welcome_ir_audio  = 1;
        cfg->doorbells[i].welcome_key_audio = 7;
        cfg->doorbells[i].doorbell_ir_audio  = 1;
        cfg->doorbells[i].doorbell_key_audio = 1;
        cfg->doorbells[i].alarm_ir_audio  = 6;
        cfg->doorbells[i].alarm_key_audio = 8;
    }
}

static void HexToBytes(const char *hex, uint8_t *out, int len)
{
    for (int i = 0; i < len; i++)
    {
        unsigned int v = 0;
        sscanf(hex + i * 2, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static void BytesToHex(const uint8_t *in, int len, char *out)
{
    for (int i = 0; i < len; i++)
    {
        sprintf(out + i * 2, "%02X", in[i]);
    }
    out[len * 2] = '\0';
}

static char* ConfigToJson(const SysConfig_t *cfg)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    cJSON *chirp = cJSON_CreateObject();
    cJSON_AddNumberToObject(chirp, "local_addr", cfg->local_addr);
    cJSON_AddNumberToObject(chirp, "netid", cfg->netid);
    cJSON_AddNumberToObject(chirp, "tx_power", cfg->tx_power);
    cJSON_AddNumberToObject(chirp, "channel", cfg->channel);
    cJSON_AddNumberToObject(chirp, "peer_addr", cfg->peer_addr);

    char xtea_hex[33];
    BytesToHex(cfg->xtea_key, 16, xtea_hex);
    cJSON_AddStringToObject(chirp, "xtea_key", xtea_hex);

    cJSON_AddItemToObject(root, "chirp", chirp);

    cJSON *boot = cJSON_CreateObject();
    cJSON_AddNumberToObject(boot, "play", cfg->boot_play ? 1 : 0);
    cJSON_AddItemToObject(root, "boot_audio", boot);

    cJSON_AddNumberToObject(root, "volume", cfg->volume ? cfg->volume : 70);
    cJSON_AddNumberToObject(root, "led_normal_on", cfg->led_normal_on ? 1 : 0);
    cJSON_AddNumberToObject(root, "led_brightness", cfg->led_brightness ? cfg->led_brightness : 50);

    /* 登录密码 */
    cJSON_AddStringToObject(root, "login_password",
        cfg->login_password[0] ? cfg->login_password : DEFAULT_LOGIN_PASSWORD);

    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi, "mode", cfg->wifi_mode == 0 ? "ap" : "station");
    cJSON_AddStringToObject(wifi, "ssid", cfg->ssid);
    cJSON_AddStringToObject(wifi, "password", cfg->password);
    cJSON_AddStringToObject(wifi, "ip_mode", cfg->ip_mode == 0 ? "dhcp" : "static");
    cJSON_AddStringToObject(wifi, "ip", cfg->ip);
    cJSON_AddStringToObject(wifi, "netmask", cfg->netmask);
    cJSON_AddStringToObject(wifi, "gateway", cfg->gateway);
    cJSON_AddStringToObject(wifi, "dns1", cfg->dns1);
    cJSON_AddStringToObject(wifi, "dns2", cfg->dns2);
    cJSON_AddStringToObject(wifi, "ntp_server",
        cfg->ntp_server[0] ? cfg->ntp_server : "ntp.aliyun.com");
    cJSON_AddItemToObject(root, "wifi", wifi);

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
    cJSON_AddItemToObject(root, "audio_notes", notes);

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

static void JsonToConfig(const char *json_str, SysConfig_t *cfg)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL)
    {
        ESP_LOGW(TAG, "JSON parse failed");
        return;
    }

    cJSON *chirp = cJSON_GetObjectItem(root, "chirp");
    if (cJSON_IsObject(chirp))
    {
        cJSON *v;
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(chirp, "local_addr"))) cfg->local_addr = v->valueint;
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(chirp, "netid")))      cfg->netid = v->valueint;
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(chirp, "tx_power")))   cfg->tx_power = v->valueint;
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(chirp, "channel")))    cfg->channel = v->valueint;
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(chirp, "peer_addr")))  cfg->peer_addr = v->valueint;

        cJSON *key = cJSON_GetObjectItem(chirp, "xtea_key");
        if (cJSON_IsString(key) && strlen(key->valuestring) == 32)
        {
            HexToBytes(key->valuestring, cfg->xtea_key, 16);
        }
    }

    cJSON *boot = cJSON_GetObjectItem(root, "boot_audio");
    if (cJSON_IsObject(boot))
    {
        cJSON *v;
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(boot, "play")))
            cfg->boot_play = (v->valueint != 0) ? 1 : 0;
    }

    cJSON *vol = cJSON_GetObjectItem(root, "volume");
    if (cJSON_IsNumber(vol)) {
        int v = vol->valueint;
        if (v < 10) v = 10;
        if (v > 100) v = 100;
        cfg->volume = (uint8_t)v;
    }

    cJSON *led = cJSON_GetObjectItem(root, "led_normal_on");
    if (cJSON_IsNumber(led)) {
        cfg->led_normal_on = (led->valueint != 0) ? 1 : 0;
    }

    cJSON *led_bri = cJSON_GetObjectItem(root, "led_brightness");
    if (cJSON_IsNumber(led_bri)) {
        int v = led_bri->valueint;
        if (v < 10) v = 10;
        if (v > 100) v = 100;
        cfg->led_brightness = (uint8_t)v;
    }

    /* 登录密码：合法非空才覆盖，否则保留默认 */
    cJSON *lp = cJSON_GetObjectItem(root, "login_password");
    if (cJSON_IsString(lp) && lp->valuestring[0] != '\0')
    {
        strncpy(cfg->login_password, lp->valuestring,
                sizeof(cfg->login_password) - 1);
        cfg->login_password[sizeof(cfg->login_password) - 1] = '\0';
    }

    cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
    if (cJSON_IsObject(wifi))
    {
        cJSON *v;
        if (cJSON_IsString(v = cJSON_GetObjectItem(wifi, "mode")))
            cfg->wifi_mode = (strcmp(v->valuestring, "station") == 0) ? 1 : 0;
        if (cJSON_IsString(v = cJSON_GetObjectItem(wifi, "ssid")))
            strncpy(cfg->ssid, v->valuestring, sizeof(cfg->ssid) - 1);
        if (cJSON_IsString(v = cJSON_GetObjectItem(wifi, "password")))
            strncpy(cfg->password, v->valuestring, sizeof(cfg->password) - 1);
        if (cJSON_IsString(v = cJSON_GetObjectItem(wifi, "ip_mode")))
            cfg->ip_mode = (strcmp(v->valuestring, "static") == 0) ? 1 : 0;
        if (cJSON_IsString(v = cJSON_GetObjectItem(wifi, "ip")))
            strncpy(cfg->ip, v->valuestring, sizeof(cfg->ip) - 1);
        if (cJSON_IsString(v = cJSON_GetObjectItem(wifi, "netmask")))
            strncpy(cfg->netmask, v->valuestring, sizeof(cfg->netmask) - 1);
        if (cJSON_IsString(v = cJSON_GetObjectItem(wifi, "gateway")))
            strncpy(cfg->gateway, v->valuestring, sizeof(cfg->gateway) - 1);
        if (cJSON_IsString(v = cJSON_GetObjectItem(wifi, "dns1")))
            strncpy(cfg->dns1, v->valuestring, sizeof(cfg->dns1) - 1);
        if (cJSON_IsString(v = cJSON_GetObjectItem(wifi, "dns2")))
            strncpy(cfg->dns2, v->valuestring, sizeof(cfg->dns2) - 1);
        if (cJSON_IsString(v = cJSON_GetObjectItem(wifi, "ntp_server")) && v->valuestring[0] != '\0')
            strncpy(cfg->ntp_server, v->valuestring, sizeof(cfg->ntp_server) - 1);
    }

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

    cJSON *notes = cJSON_GetObjectItem(root, "audio_notes");
    if (cJSON_IsObject(notes))
    {
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
}

static esp_err_t ConfigPartitionRegister(void)
{
    if (s_config_part != NULL) return ESP_OK;

    esp_err_t err = esp_partition_register_external(
        esp_flash_default_chip,
        CONFIG_FLASH_OFFSET,
        CONFIG_FLASH_SIZE,
        CONFIG_PART_LABEL,
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        &s_config_part
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "register external partition failed: %s", esp_err_to_name(err));
        s_config_part = NULL;
        return err;
    }

    ESP_LOGI(TAG, "config partition registered, addr=0x%lx size=%lu",
             s_config_part->address, s_config_part->size);
    return ESP_OK;
}

void AppConfig_Init(void)
{
    ESP_LOGI(TAG, "AppConfig init");

    LoadDefaultValues(&s_config);

    if (ConfigPartitionRegister() != ESP_OK)
    {
        ESP_LOGE(TAG, "config partition not available, using defaults (no save)");
        return;
    }

    if (AppConfig_Load() == ESP_OK)
    {
        ESP_LOGI(TAG, "Loaded config from external partition");
    }
    else
    {
        ESP_LOGI(TAG, "No config in partition, using defaults, saving...");
        AppConfig_Save();
    }
}

SysConfig_t* AppConfig_Get(void)
{
    return &s_config;
}

esp_err_t AppConfig_Save(void)
{
    if (s_config_part == NULL)
    {
        ESP_LOGE(TAG, "config partition not registered");
        return ESP_ERR_INVALID_STATE;
    }

    char *json = ConfigToJson(&s_config);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "ConfigToJson failed");
        return ESP_FAIL;
    }

    size_t json_len = strlen(json);
    if (json_len > CONFIG_JSON_MAX)
    {
        ESP_LOGE(TAG, "JSON too long: %d", (int)json_len);
        free(json);
        return ESP_FAIL;
    }

    size_t total = CONFIG_HEADER_SIZE + json_len;
    uint8_t *buf = malloc(total);
    if (buf == NULL)
    {
        ESP_LOGE(TAG, "malloc %d failed", (int)total);
        free(json);
        return ESP_ERR_NO_MEM;
    }
    memset(buf, 0, total);

    buf[0] = (CONFIG_MAGIC >> 8) & 0xFF;
    buf[1] = (CONFIG_MAGIC >> 0) & 0xFF;
    buf[2] = 0x00;
    buf[3] = 0x01;
    buf[4] = (json_len >> 8) & 0xFF;
    buf[5] = (json_len >> 0) & 0xFF;

    memcpy(buf + CONFIG_HEADER_SIZE, json, json_len);

    esp_err_t err = esp_partition_erase_range(s_config_part, 0, CONFIG_ERASE_SIZE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "erase failed: %s", esp_err_to_name(err));
        free(buf);
        free(json);
        return err;
    }

    err = esp_partition_write(s_config_part, 0, buf, total);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "write failed: %s", esp_err_to_name(err));
        free(buf);
        free(json);
        return err;
    }

    ESP_LOGI(TAG, "Config saved (%d bytes JSON, %d total)", (int)json_len, (int)total);
    free(buf);
    free(json);
    return ESP_OK;
}

esp_err_t AppConfig_Load(void)
{
    if (s_config_part == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t header[CONFIG_HEADER_SIZE];
    esp_err_t err = esp_partition_read(s_config_part, 0, header, CONFIG_HEADER_SIZE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "read header failed: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t magic = ((uint16_t)header[0] << 8) | header[1];
    if (magic != CONFIG_MAGIC)
    {
        ESP_LOGW(TAG, "magic mismatch: 0x%04X (expect 0x%04X)", magic, CONFIG_MAGIC);
        return ESP_ERR_NOT_FOUND;
    }

    uint16_t json_len = ((uint16_t)header[4] << 8) | header[5];
    if (json_len == 0 || json_len > CONFIG_JSON_MAX)
    {
        ESP_LOGW(TAG, "invalid json_len: %d", json_len);
        return ESP_ERR_INVALID_SIZE;
    }

    char *json = malloc(json_len + 1);
    if (json == NULL) return ESP_ERR_NO_MEM;

    err = esp_partition_read(s_config_part, CONFIG_HEADER_SIZE, json, json_len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "read json failed: %s", esp_err_to_name(err));
        free(json);
        return err;
    }
    json[json_len] = '\0';

    LoadDefaultValues(&s_config);
    JsonToConfig(json, &s_config);

    ESP_LOGI(TAG, "Config loaded (%d bytes JSON)", json_len);
    free(json);
    return ESP_OK;
}

void AppConfig_ResetToDefault(void)
{
    LoadDefaultValues(&s_config);
    AppConfig_Save();
    ESP_LOGI(TAG, "Config reset to default");
}