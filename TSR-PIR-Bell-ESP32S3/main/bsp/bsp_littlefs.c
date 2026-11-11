#include "bsp_littlefs.h"
#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "BSP_LITTLEFS";

esp_err_t LittleFsInit(void)
{
    ESP_LOGI(TAG, "Initializing LittleFS (audio)");

    esp_vfs_littlefs_conf_t conf = {
        .base_path = LITTLEFS_MOUNT_POINT,      /* 仍用 /littlefs？还是改成 /audio？ */
        .partition_label = "audio",             /* ← 从 littlefs 改成 audio */
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount LittleFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    return ESP_OK;
}