/**
 * @file bsp_player_pcm.cpp
 * @brief PCM 直读播放（不依赖队列，供 PlayerPlayOnce 分派调用）
 */
#include "bsp_player_internal.h"
#include "bsp_es8311.h"
#include "esp_log.h"

#include <cstdio>
#include <cstdlib>

static const char *TAG = "BSP_PLAYER_PCM";

#define PLAYER_CHUNK_SIZE      4096

extern "C" esp_err_t PlayerPlayPcmInternal(const char *path)
{
    FILE *fd = fopen(path, "rb");
    if (fd == NULL) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return ESP_FAIL;
    }

    uint8_t *chunk = (uint8_t *)malloc(PLAYER_CHUNK_SIZE);
    if (chunk == NULL) {
        ESP_LOGE(TAG, "malloc %d failed", PLAYER_CHUNK_SIZE);
        fclose(fd);
        return ESP_FAIL;
    }

    size_t total_read = 0;
    size_t n;
    while ((n = fread(chunk, 1, PLAYER_CHUNK_SIZE, fd)) > 0) {
        if (Es8311PlayPcm(chunk, n) != ESP_OK) {
            ESP_LOGE(TAG, "Play chunk failed");
            free(chunk);
            fclose(fd);
            return ESP_FAIL;
        }
        total_read += n;
    }

    free(chunk);
    fclose(fd);
    ESP_LOGI(TAG, "%s played %u bytes", path, (unsigned)total_read);
    return ESP_OK;
}