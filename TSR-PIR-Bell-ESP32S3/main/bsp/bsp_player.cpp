/**
 * @file bsp_player.cpp
 * @brief 播放器外壳：队列 / 播放任务 / 对外接口 / 分派 / 同步异步校验
 *
 * PCM 与 MP3 具体播放实现分别在：
 *   bsp_player_pcm.cpp  PlayerPlayPcmInternal
 *   bsp_player_mp3.cpp  PlayerPlayMp3Internal
 */
#include "bsp_player.h"
#include "bsp_player_internal.h"
#include "bsp_littlefs.h"
#include "bsp_es8311.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char *TAG = "BSP_PLAYER";

/* 异步校验任务栈大小（覆盖解码器 + I2S 写链峰值） */
#define PLAYER_SYNC_TASK_STACK 12288

/* ===== 队列元素 ===== */
typedef struct {
    char          filename[PLAYER_FILENAME_MAX];
    PlayerOwner_t owner;
    uint16_t      src;        /* 仅 CHIRP 有效：分机地址 */
    uint8_t       is_key;     /* 仅 CHIRP 有效：1=按键 0=PIR */
} PlayerQueueItem_t;

static SemaphoreHandle_t s_play_sem  = NULL;
static bool              s_ready     = false;

/* 当前正在播的文件 / 来源 */
static char          s_current_file[PLAYER_FILENAME_MAX];
static PlayerOwner_t s_current_owner = PLAYER_OWNER_NONE;
static volatile bool s_playing       = false;

/* 批量入队模式：Begin 后 PlayerTryPlay 不 give 信号量，End 时统一唤醒 */
static volatile bool     s_batch_mode = false;

/* 待播队列（独立于 Chirp 报文 FIFO） */
static PlayerQueueItem_t s_queue[PLAYER_QUEUE_DEPTH];
static int               s_q_head = 0;   /* 取 */
static int               s_q_tail = 0;   /* 放 */
static int               s_q_count = 0;
static SemaphoreHandle_t s_q_lock = NULL;

/* ===== 队列操作（调用方持锁） ===== */
static int PlayerQueueCount(void)
{
    return s_q_count;
}

static void PlayerQueueClear(void)
{
    s_q_head = s_q_tail = s_q_count = 0;
}

/* 队尾追加；满了丢队尾最后一个（替换） */
static void PlayerQueuePushTail(const char *filename, PlayerOwner_t owner,
                                uint16_t src, uint8_t is_key)
{
    if (s_q_count >= PLAYER_QUEUE_DEPTH) {
        ESP_LOGW(TAG, "queue full, drop tail: %s (owner=%d)",
                 s_queue[(s_q_tail + PLAYER_QUEUE_DEPTH - 1) % PLAYER_QUEUE_DEPTH].filename,
                 s_queue[(s_q_tail + PLAYER_QUEUE_DEPTH - 1) % PLAYER_QUEUE_DEPTH].owner);
        s_q_tail = (s_q_tail + PLAYER_QUEUE_DEPTH - 1) % PLAYER_QUEUE_DEPTH;
        s_q_count--;
    }
    PlayerQueueItem_t *it = &s_queue[s_q_tail];
    strncpy(it->filename, filename, sizeof(it->filename) - 1);
    it->filename[sizeof(it->filename) - 1] = '\0';
    it->owner  = owner;
    it->src    = src;
    it->is_key = is_key;
    s_q_tail = (s_q_tail + 1) % PLAYER_QUEUE_DEPTH;
    s_q_count++;
}

/* 队头插入（CONFIG 最高优先级用，保证同批 CONFIG 连续排在最前） */
static void PlayerQueuePushHead(const char *filename, PlayerOwner_t owner,
                                uint16_t src, uint8_t is_key)
{
    if (s_q_count >= PLAYER_QUEUE_DEPTH) {
        ESP_LOGW(TAG, "queue full, drop tail");
        s_q_tail = (s_q_tail + PLAYER_QUEUE_DEPTH - 1) % PLAYER_QUEUE_DEPTH;
        s_q_count--;
    }
    s_q_head = (s_q_head + PLAYER_QUEUE_DEPTH - 1) % PLAYER_QUEUE_DEPTH;
    PlayerQueueItem_t *it = &s_queue[s_q_head];
    strncpy(it->filename, filename, sizeof(it->filename) - 1);
    it->filename[sizeof(it->filename) - 1] = '\0';
    it->owner  = owner;
    it->src    = src;
    it->is_key = is_key;
    s_q_count++;
}

/* 在"跳过前 N 个元素"的位置插入（N 由调用方算好） */
static void PlayerQueueInsertAt(int rel_pos, const char *filename,
                                PlayerOwner_t owner, uint16_t src, uint8_t is_key)
{
    if (rel_pos < 0) rel_pos = 0;
    if (rel_pos > s_q_count) rel_pos = s_q_count;

    if (s_q_count >= PLAYER_QUEUE_DEPTH) {
        ESP_LOGW(TAG, "queue full, drop tail");
        s_q_tail = (s_q_tail + PLAYER_QUEUE_DEPTH - 1) % PLAYER_QUEUE_DEPTH;
        s_q_count--;
        if (rel_pos > s_q_count) rel_pos = s_q_count;
    }

    /* 从队尾往 rel_pos 方向整体后移一位 */
    for (int i = s_q_count; i > rel_pos; i--) {
        int dst = (s_q_head + i) % PLAYER_QUEUE_DEPTH;
        int src_idx = (s_q_head + i - 1) % PLAYER_QUEUE_DEPTH;
        s_queue[dst] = s_queue[src_idx];
    }
    int pos = (s_q_head + rel_pos) % PLAYER_QUEUE_DEPTH;
    PlayerQueueItem_t *it = &s_queue[pos];
    strncpy(it->filename, filename, sizeof(it->filename) - 1);
    it->filename[sizeof(it->filename) - 1] = '\0';
    it->owner  = owner;
    it->src    = src;
    it->is_key = is_key;
    s_q_tail = (s_q_tail + 1) % PLAYER_QUEUE_DEPTH;
    s_q_count++;
}

/* 计算"跳过所有 CONFIG 元素"后的相对下标：返回前导 CONFIG 段之后的位置 */
static int PlayerQueueConfigEndPos(void)
{
    int n = 0;
    for (int i = 0; i < s_q_count; i++) {
        int idx = (s_q_head + i) % PLAYER_QUEUE_DEPTH;
        if (s_queue[idx].owner == PLAYER_OWNER_CONFIG) {
            n = i + 1;
        } else {
            break;  /* CONFIG 段连续在最前，遇到第一个非 CONFIG 即停 */
        }
    }
    return n;
}

/* 插入到第一个 KEY 之前（WEB 用），但必须在 CONFIG 段之后 */
static void PlayerQueuePushBeforeKey(const char *filename, PlayerOwner_t owner,
                                     uint16_t src, uint8_t is_key)
{
    int base = PlayerQueueConfigEndPos();

    int insert_pos = -1;
    for (int i = base; i < s_q_count; i++) {
        int idx = (s_q_head + i) % PLAYER_QUEUE_DEPTH;
        if (s_queue[idx].owner == PLAYER_OWNER_KEY) {
            insert_pos = i;
            break;
        }
    }

    if (insert_pos < 0) {
        PlayerQueuePushTail(filename, owner, src, is_key);
        return;
    }
    PlayerQueueInsertAt(insert_pos, filename, owner, src, is_key);
}

/* CHIRP 用：插到 CONFIG 段之后、其它元素之前 */
static void PlayerQueuePushAfterConfig(const char *filename, PlayerOwner_t owner,
                                       uint16_t src, uint8_t is_key)
{
    int pos = PlayerQueueConfigEndPos();
    PlayerQueueInsertAt(pos, filename, owner, src, is_key);
}

static bool PlayerQueuePop(PlayerQueueItem_t *out)
{
    if (s_q_count <= 0) return false;
    *out = s_queue[s_q_head];
    s_q_head = (s_q_head + 1) % PLAYER_QUEUE_DEPTH;
    s_q_count--;
    return true;
}

/* CHIRP 去重：队列中已有相同 (src, is_key) 就返回 true */
static bool PlayerQueueHasChirp(uint16_t src, uint8_t is_key)
{
    for (int i = 0; i < s_q_count; i++) {
        int idx = (s_q_head + i) % PLAYER_QUEUE_DEPTH;
        if (s_queue[idx].owner == PLAYER_OWNER_CHIRP &&
            s_queue[idx].src == src &&
            s_queue[idx].is_key == is_key) {
            return true;
        }
    }
    return false;
}

/* ===== 判断扩展名 ===== */
static bool HasExt(const char *path, const char *ext)
{
    size_t plen = strlen(path);
    size_t elen = strlen(ext);
    if (plen < elen) return false;
    for (size_t i = 0; i < elen; i++) {
        char a = path[plen - elen + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

/* ===== 按扩展名分派到 PCM / MP3 实现 ===== */
static esp_err_t PlayerPlayOnce(const char *filename)
{
    char path[160];
    snprintf(path, sizeof(path), "%s%s", LITTLEFS_MOUNT_POINT, filename);

    if (HasExt(path, ".mp3")) {
        return PlayerPlayMp3Internal(path);
    }
    return PlayerPlayPcmInternal(path);
}

/* ===== 同步播放（在调用者任务栈上跑，仅供非 httpd 场景） ===== */
extern "C" esp_err_t PlayerPlayFileSync(const char *filename)
{
    if (!s_ready || filename == NULL || filename[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = PlayerPlayOnce(filename);
    ESP_LOGI(TAG, "[sync] %s -> %s", filename, esp_err_to_name(ret));
    return ret;
}

/* ===== 异步校验播放（内部一次性任务，调用者阻塞等结果） ===== */
typedef struct {
    const char         *filename;   /* 指向调用者栈上缓冲，任务运行期间有效 */
    SemaphoreHandle_t   done;
    volatile esp_err_t  ret;
} SyncPlayCtx_t;

static void PlayerSyncPlayTask(void *arg)
{
    SyncPlayCtx_t *ctx = (SyncPlayCtx_t *)arg;

    esp_err_t ret = ESP_FAIL;
    if (s_ready && ctx->filename != NULL && ctx->filename[0] != '\0') {
        ret = PlayerPlayOnce(ctx->filename);
    } else {
        ret = ESP_ERR_INVALID_STATE;
    }
    ctx->ret = ret;

    xSemaphoreGive(ctx->done);
    vTaskDelete(NULL);
}

extern "C" esp_err_t PlayerPlayFileSyncEx(const char *filename, uint32_t timeout_ms)
{
    if (filename == NULL || filename[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    SyncPlayCtx_t ctx;
    ctx.filename = filename;
    ctx.ret      = ESP_FAIL;
    ctx.done     = xSemaphoreCreateBinary();
    if (ctx.done == NULL) {
        ESP_LOGE(TAG, "[sync_ex] create semaphore failed");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(PlayerSyncPlayTask, "sync_play",
                                PLAYER_SYNC_TASK_STACK, &ctx, 5, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "[sync_ex] create task failed");
        vSemaphoreDelete(ctx.done);
        return ESP_FAIL;
    }

    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(ctx.done, ticks) != pdTRUE) {
        ESP_LOGW(TAG, "[sync_ex] %s timeout (%u ms)", filename, (unsigned)timeout_ms);
        /* 超时极罕见（30s 覆盖 200KB/24kHz ≈ 20s）；任务可能还在跑，
         * ctx 在栈上已失效，记录并返回。 */
        vSemaphoreDelete(ctx.done);
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ctx.ret;
    vSemaphoreDelete(ctx.done);
    ESP_LOGI(TAG, "[sync_ex] %s -> %s", filename, esp_err_to_name(ret));
    return ret;
}

/* ===== 播放任务 ===== */
static void PlayerTask(void *args)
{
    (void)args;

    while (1) {
        if (xSemaphoreTake(s_play_sem, portMAX_DELAY) != pdTRUE) continue;
        if (!s_ready) continue;

        PlayerQueueItem_t item;
        bool have_item = false;

        xSemaphoreTake(s_q_lock, portMAX_DELAY);
        if (PlayerQueuePop(&item)) {
            have_item = true;
        }
        xSemaphoreGive(s_q_lock);

        if (!have_item) {
            /* 信号量被 give 但队列空（例如 PlayerTrigger 直接 give），
             * 用 s_current_file 作兜底 */
            strncpy(item.filename, s_current_file, sizeof(item.filename) - 1);
            item.filename[sizeof(item.filename) - 1] = '\0';
            item.owner  = s_current_owner;
            item.src    = 0;
            item.is_key = 0;
        }

        s_playing = true;
        strncpy(s_current_file, item.filename, sizeof(s_current_file) - 1);
        s_current_file[sizeof(s_current_file) - 1] = '\0';
        s_current_owner = item.owner;

        ESP_LOGI(TAG, "[player] start playing %s (owner=%d, src=0x%04X, key=%u)",
                 item.filename, (int)item.owner, item.src, item.is_key);

        /* 打开 PA（对非 MP3 路径生效；MP3 路径内会按文件参数覆盖） */
        Es8311PaOn();

        if (PlayerPlayOnce(item.filename) == ESP_OK) {
            ESP_LOGI(TAG, "[player] play done");
        } else {
            ESP_LOGE(TAG, "[player] play failed");
        }

        Es8311PaOff();

        s_playing = false;
        s_current_owner = PLAYER_OWNER_NONE;

        xSemaphoreTake(s_q_lock, portMAX_DELAY);
        bool more = (PlayerQueueCount() > 0);
        xSemaphoreGive(s_q_lock);
        if (more) {
            xSemaphoreGive(s_play_sem);
        }
    }
}

/* ===== 对外接口 ===== */
extern "C" esp_err_t PlayerInit(void)
{
    s_play_sem = xSemaphoreCreateBinary();
    if (s_play_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return ESP_FAIL;
    }

    s_q_lock = xSemaphoreCreateMutex();
    if (s_q_lock == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        vSemaphoreDelete(s_play_sem);
        s_play_sem = NULL;
        return ESP_FAIL;
    }

    PlayerQueueClear();
    strncpy(s_current_file, PLAYER_DEFAULT_FILE, sizeof(s_current_file) - 1);

    BaseType_t ret = xTaskCreate(PlayerTask, "player_task", 12288, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create player task");
        vSemaphoreDelete(s_q_lock);
        vSemaphoreDelete(s_play_sem);
        s_q_lock = NULL;
        s_play_sem = NULL;
        return ESP_FAIL;
    }

    s_ready = true;
    ESP_LOGI(TAG, "Player init done (default=%s, queue=%d)",
             PLAYER_DEFAULT_FILE, PLAYER_QUEUE_DEPTH);
    return ESP_OK;
}

extern "C" void PlayerTrigger(const char *filename)
{
    if (s_play_sem == NULL) return;

    xSemaphoreTake(s_q_lock, portMAX_DELAY);
    const char *name = (filename != NULL && filename[0] != '\0')
                       ? filename : PLAYER_DEFAULT_FILE;
    PlayerQueuePushHead(name, PLAYER_OWNER_CHIRP, 0, 0);
    xSemaphoreGive(s_q_lock);

    xSemaphoreGive(s_play_sem);
}

extern "C" bool PlayerTryPlay(const char *filename, PlayerOwner_t owner,
                              uint16_t src, uint8_t is_key)
{
    if (s_play_sem == NULL || s_q_lock == NULL) return false;

    const char *name = (filename != NULL && filename[0] != '\0')
                       ? filename : PLAYER_DEFAULT_FILE;

    xSemaphoreTake(s_q_lock, portMAX_DELAY);

    if (owner == PLAYER_OWNER_CHIRP) {
        if (PlayerQueueHasChirp(src, is_key)) {
            xSemaphoreGive(s_q_lock);
            ESP_LOGI(TAG, "[queue] CHIRP dup (src=0x%04X, key=%u), skip",
                     src, is_key);
            return true;
        }
    }

    bool idle = (!s_playing && PlayerQueueCount() == 0);

    if (idle) {
        PlayerQueuePushHead(name, owner, src, is_key);
    } else {
        switch (owner) {
            case PLAYER_OWNER_CONFIG:
                PlayerQueuePushHead(name, owner, src, is_key);
                break;
            case PLAYER_OWNER_CHIRP:
                PlayerQueuePushAfterConfig(name, owner, src, is_key);
                break;
            case PLAYER_OWNER_WEB:
                PlayerQueuePushBeforeKey(name, owner, src, is_key);
                break;
            case PLAYER_OWNER_KEY:
            default:
                PlayerQueuePushTail(name, owner, src, is_key);
                break;
        }
    }

    xSemaphoreGive(s_q_lock);

    if (!s_batch_mode) {
        xSemaphoreGive(s_play_sem);
    }

    ESP_LOGI(TAG, "[queue] enqueue %s (owner=%d, src=0x%04X, key=%u, idle=%d, batch=%d)",
             name, (int)owner, src, is_key, (int)idle, (int)s_batch_mode);
    return true;
}

extern "C" bool PlayerIsReady(void)
{
    return s_ready;
}

extern "C" bool PlayerIsPlaying(void)
{
    return s_playing;
}

extern "C" void PlayerBatchBegin(void)
{
    if (s_q_lock == NULL) return;
    xSemaphoreTake(s_q_lock, portMAX_DELAY);
    s_batch_mode = true;
    xSemaphoreGive(s_q_lock);
    ESP_LOGI(TAG, "batch begin");
}

extern "C" void PlayerBatchEnd(void)
{
    if (s_q_lock == NULL || s_play_sem == NULL) return;
    xSemaphoreTake(s_q_lock, portMAX_DELAY);
    s_batch_mode = false;
    bool more = (PlayerQueueCount() > 0);
    xSemaphoreGive(s_q_lock);
    if (more) {
        xSemaphoreGive(s_play_sem);
    }
    ESP_LOGI(TAG, "batch end (queue=%d, wake=%d)", (int)PlayerQueueCount(), (int)more);
}