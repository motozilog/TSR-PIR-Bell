/**
 * @file app_ip_announce.c
 * @brief 内机 WiFi 获取 IP 后语音播报（如 "IP地址是 1 9 2 点 1 6 8 点 1 0 0 点 2 1 3"）
 *
 * 素材（LittleFS 根目录）：
 *   ipadd.mp3        固定前缀"IP地址是"
 *   ipadd0~9.mp3     数字 0~9
 *   ipaddpoint.mp3   "点"
 *
 * 触发：WifiStaEventHandler GOT_IP 且 IP 字符串变化 → s_ip_announce_pending
 * 消费：本任务 100ms 轮询 WifiStationTakePendingIp，拆 IP 片段后
 *       用 PlayerBatchBegin/End 包裹批量入队（CONFIG 最高优先级，倒序入队保证正序播放），
 *       其它事件只能排在 IP 片段之后，IP 播报完整不被打断。
 */
#include "app_ip_announce.h"
#include "bsp_wifistation.h"
#include "bsp_player.h"
#include "bsp_littlefs.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "APP_IP_ANNOUNCE";

/* 素材文件名（相对 LittleFS 挂载点） */
#define IP_FIXED_FILE   "/ipadd.mp3"
#define IP_POINT_FILE   "/ipaddpoint.mp3"

/* 最大片段数：1 前缀 + 4 段×3 位 + 3 点 = 16，留余量给 20 */
#define IP_MAX_CLIPS    20

/* 检查单个素材是否存在 */
static bool IpAudioExists(const char *filename)
{
    char path[64];
    snprintf(path, sizeof(path), "%s%s", LITTLEFS_MOUNT_POINT, filename);
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

/* 检查全部素材（ipadd + ipaddpoint + ipadd0~9，共 12 个）是否齐全 */
static bool IpAudioAllExist(void)
{
    if (!IpAudioExists(IP_FIXED_FILE)) {
        ESP_LOGW(TAG, "missing %s", IP_FIXED_FILE);
        return false;
    }
    if (!IpAudioExists(IP_POINT_FILE)) {
        ESP_LOGW(TAG, "missing %s", IP_POINT_FILE);
        return false;
    }
    for (int d = 0; d <= 9; d++) {
        char fn[16];
        snprintf(fn, sizeof(fn), "/ipadd%d.mp3", d);
        if (!IpAudioExists(fn)) {
            ESP_LOGW(TAG, "missing %s", fn);
            return false;
        }
    }
    return true;
}

/* 把 IP 字符串拆成片段文件名列表；返回片段数（0 表示失败）
 * 例：192.168.100.213 ->
 *   /ipadd.mp3 /ipadd1.mp3 /ipadd9.mp3 /ipadd2.mp3 /ipaddpoint.mp3
 *   /ipadd1.mp3 /ipadd6.mp3 /ipadd8.mp3 /ipaddpoint.mp3
 *   /ipadd1.mp3 /ipadd0.mp3 /ipadd0.mp3 /ipaddpoint.mp3
 *   /ipadd2.mp3 /ipadd1.mp3 /ipadd3.mp3
 */
static int IpBuildClips(const char *ip, char clips[IP_MAX_CLIPS][16])
{
    int n = 0;

    /* 固定前缀 */
    if (n >= IP_MAX_CLIPS) return 0;
    snprintf(clips[n++], 16, "%s", IP_FIXED_FILE);

    const char *p = ip;
    while (*p != '\0' && n < IP_MAX_CLIPS) {
        if (*p == '.') {
            snprintf(clips[n++], 16, "%s", IP_POINT_FILE);
            p++;
            continue;
        }

        /* 收集连续数字段 */
        int val = 0;
        bool has_digit = false;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
            has_digit = true;
        }
        if (!has_digit) break;  /* 非法字符，停止 */
        if (val < 0 || val > 255) val = 0;  /* 防御 */

        /* 逐位拆 */
        char digits[4];
        int len = snprintf(digits, sizeof(digits), "%d", val);
        for (int i = 0; i < len && n < IP_MAX_CLIPS; i++) {
            int d = digits[i] - '0';
            if (d < 0 || d > 9) continue;
            snprintf(clips[n++], 16, "/ipadd%d.mp3", d);
        }
    }
    return n;
}

static void IpAnnounceTask(void *args)
{
    (void)args;

    char ip[16];
    bool assets_checked = false;
    bool assets_ok = false;

    while (1) {
        if (WifiStationTakePendingIp(ip, sizeof(ip))) {
            /* 首次播报才检查素材，避免开机无谓扫文件 */
            if (!assets_checked) {
                assets_ok = IpAudioAllExist();
                assets_checked = true;
                if (!assets_ok) {
                    ESP_LOGW(TAG, "IP audio assets incomplete, skip all announcements");
                }
            }
            if (!assets_ok) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            char clips[IP_MAX_CLIPS][16];
            int n = IpBuildClips(ip, clips);
            if (n <= 0) {
                ESP_LOGW(TAG, "build clips failed for %s", ip);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            ESP_LOGI(TAG, "announce IP %s: %d clips", ip, n);

            /* 批量模式：Begin 后 PlayerTryPlay 只入队不唤醒 player_task，
             * 倒序入队（CONFIG 走 PushHead 插队头），End 时统一唤醒，
             * 保证从 clips[0] 开始正序播放，且第一片段不会被 idle 分支抢播。 */
            PlayerBatchBegin();
            for (int i = n - 1; i >= 0; i--) {
                PlayerTryPlay(clips[i], PLAYER_OWNER_CONFIG, 0, 0);
            }
            PlayerBatchEnd();
            ESP_LOGI(TAG, "announce queued: %d clips", n);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t AppIpAnnounceInit(void)
{
    BaseType_t ret = xTaskCreate(IpAnnounceTask, "ip_announce", 4096, NULL, 3, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "create ip_announce task failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ip_announce task started");
    return ESP_OK;
}