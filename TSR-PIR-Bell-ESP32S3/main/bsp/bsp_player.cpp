#include "bsp_player.h"
#include "bsp_littlefs.h"
#include "bsp_es8311.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "micro_decoder/decoder_source.h"
#include "micro_decoder/types.h"

static const char *TAG = "BSP_PLAYER";

#define PLAYER_CHUNK_SIZE      4096

/* 单声道补双声道用的临时缓冲，放文件级避免占 player_task 栈 */
static uint8_t s_stereo_buf[4096];

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
        /* 丢队尾最后一个 */
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

/* 队头插入（CHIRP 高优先级） */
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

/* 插入到第一个 KEY 之前（WEB 插在 KEY 之前、CHIRP 之后） */
static void PlayerQueuePushBeforeKey(const char *filename, PlayerOwner_t owner,
                                     uint16_t src, uint8_t is_key)
{
    if (s_q_count >= PLAYER_QUEUE_DEPTH) {
        ESP_LOGW(TAG, "queue full, drop tail");
        s_q_tail = (s_q_tail + PLAYER_QUEUE_DEPTH - 1) % PLAYER_QUEUE_DEPTH;
        s_q_count--;
    }

    /* 找第一个 KEY 的位置 */
    int insert_pos = -1;
    for (int i = 0; i < s_q_count; i++) {
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

    /* 从 insert_pos 开始整体后移一位 */
    for (int i = s_q_count; i > insert_pos; i--) {
        int dst = (s_q_head + i) % PLAYER_QUEUE_DEPTH;
        int src_idx = (s_q_head + i - 1) % PLAYER_QUEUE_DEPTH;
        s_queue[dst] = s_queue[src_idx];
    }
    int pos = (s_q_head + insert_pos) % PLAYER_QUEUE_DEPTH;
    PlayerQueueItem_t *it = &s_queue[pos];
    strncpy(it->filename, filename, sizeof(it->filename) - 1);
    it->filename[sizeof(it->filename) - 1] = '\0';
    it->owner  = owner;
    it->src    = src;
    it->is_key = is_key;
    s_q_tail = (s_q_tail + 1) % PLAYER_QUEUE_DEPTH;
    s_q_count++;
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

/* ===== PCM 直读播放 ===== */
static esp_err_t PlayerPlayPcm(const char *path)
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
    ESP_LOGI(TAG, "[player] %s played %u bytes", path, (unsigned)total_read);
    return ESP_OK;
}

/* ===== MP3 解码播放（整块读 + play_buffer） ===== */
struct Mp3Sink : micro_decoder::DecoderListener {
    uint8_t  channels = 0;
    uint8_t  bits = 16;
    volatile bool write_failed = false;

    size_t on_audio_write(const uint8_t *data, size_t length, uint32_t timeout_ms) override {
        if (channels == 1) {
            /* 16bit 单声道 -> 双声道，分块处理 */
            const int16_t *in = (const int16_t *)data;
            size_t frames = length / 2;
            size_t done = 0;
            while (done < frames) {
                size_t n = frames - done;
                if (n > sizeof(s_stereo_buf) / 4) n = sizeof(s_stereo_buf) / 4;
                int16_t *out = (int16_t *)s_stereo_buf;
                for (size_t i = 0; i < n; i++) {
                    int16_t s = in[done + i];
                    out[i * 2]     = s;
                    out[i * 2 + 1] = s;
                }
                esp_err_t err = Es8311PlayPcm(s_stereo_buf, n * 4);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "[mp3] Es8311PlayPcm failed: %s", esp_err_to_name(err));
                    write_failed = true;
                    return 0;
                }
                done += n;
            }
            return length;
        }

        esp_err_t err = Es8311PlayPcm(data, length);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[mp3] Es8311PlayPcm failed: %s", esp_err_to_name(err));
            write_failed = true;
            return 0;
        }
        return length;
    }

    void on_stream_info(const micro_decoder::AudioStreamInfo &info) override {
        channels = info.get_channels();
        bits = info.get_bits_per_sample();
        ESP_LOGI(TAG, "[mp3] stream: %lu Hz, %u ch, %u bit",
                 (unsigned long)info.get_sample_rate(),
                 (unsigned)channels, (unsigned)bits);
    }

    void on_state_change(micro_decoder::DecoderState state) override {
        if (state == micro_decoder::DecoderState::FAILED) {
            ESP_LOGE(TAG, "[mp3] state -> FAILED");
        }
    }
};

/* 探测 MP3 文件头：跳过 ID3v2，找帧同步，解析采样率与声道
 * 成功返回 true 并填 rate/channels；失败返回 false
 */
static bool ProbeMp3Params(FILE *fd, uint32_t *rate, uint8_t *channels)
{
    static const uint32_t mp3_rates[3][3] = {
        {44100, 48000, 32000},   /* MPEG1 */
        {22050, 24000, 16000},   /* MPEG2 */
        {11025, 12000,  8000},   /* MPEG2.5 */
    };

    fseek(fd, 0, SEEK_SET);

    /* 跳过 ID3v2 */
    uint8_t hdr[10];
    if (fread(hdr, 1, 10, fd) == 10 &&
        hdr[0]=='I' && hdr[1]=='D' && hdr[2]=='3') {
        uint32_t tag_size = ((uint32_t)(hdr[6]&0x7F) << 21) |
                            ((uint32_t)(hdr[7]&0x7F) << 14) |
                            ((uint32_t)(hdr[8]&0x7F) << 7)  |
                            ((uint32_t)(hdr[9]&0x7F));
        fseek(fd, (long)(tag_size + 10), SEEK_SET);
    } else {
        fseek(fd, 0, SEEK_SET);
    }

    /* 找帧同步 0xFF 0xEx，最多找 4KB */
    uint8_t b[4];
    for (int i = 0; i < 4096; i++) {
        if (fread(b, 1, 4, fd) != 4) return false;
        if (b[0] == 0xFF && (b[1] & 0xE0) == 0xE0) {
            uint8_t ver   = (b[1] >> 3) & 0x03;  /* 3=MPEG1 2=MPEG2 0=MPEG2.5 */
            uint8_t srIdx = (b[2] >> 2) & 0x03;
            uint8_t chMd  = (b[3] >> 6) & 0x03;  /* 3=单声道，其它=立体声/联合 */

            if (ver == 1 || srIdx == 3) return false; /* reserved */

            uint8_t ver_row = (ver == 3) ? 0 : ((ver == 2) ? 1 : 2);
            *rate = mp3_rates[ver_row][srIdx];
            *channels = (chMd == 3) ? 1 : 2;
            fseek(fd, 0, SEEK_SET);
            return true;
        }
        fseek(fd, -3, SEEK_CUR);  /* 逐字节滑动 */
    }
    return false;
}

static esp_err_t PlayerPlayMp3(const char *path)
{
    FILE *fd = fopen(path, "rb");
    if (fd == NULL) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return ESP_FAIL;
    }

    fseek(fd, 0, SEEK_END);
    long sz = ftell(fd);
    fseek(fd, 0, SEEK_SET);
    if (sz <= 0 || sz > PLAYER_MP3_MAX_SIZE) {
        ESP_LOGE(TAG, "MP3 size %ld out of range (max %d)", sz, PLAYER_MP3_MAX_SIZE);
        fclose(fd);
        return ESP_FAIL;
    }

    /* 探测 MP3 参数并配置 codec；失败或超范围回退默认 44.1k/16/2 */
    uint32_t rate = ES8311_SAMPLE_RATE;
    uint8_t  ch   = ES8311_DEFAULT_CHANNELS;
    if (ProbeMp3Params(fd, &rate, &ch)) {
        if (rate < 8000 || rate > 96000) {
            ESP_LOGW(TAG, "[mp3] rate %lu out of range, fallback 44.1k",
                     (unsigned long)rate);
            rate = 44100;
            ch   = 2;
        }
        if (Es8311PlayConfigure(rate, ch, 16) != ESP_OK) {
            ESP_LOGW(TAG, "[mp3] configure(%lu,%u) failed, fallback 44.1k/2",
                     (unsigned long)rate, ch);
            Es8311PlayConfigure(44100, 2, 16);
            rate = 44100;
            ch   = 2;
        }
        ESP_LOGI(TAG, "[mp3] file params: %lu Hz, %u ch",
                 (unsigned long)rate, ch);
    } else {
        ESP_LOGW(TAG, "[mp3] invalid header, fallback 44.1k/2 (file may be bad)");
        Es8311PlayConfigure(44100, 2, 16);
        rate = 44100;
        ch   = 2;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (buf == NULL) {
        ESP_LOGE(TAG, "malloc %ld failed", sz);
        fclose(fd);
        return ESP_FAIL;
    }

    size_t rd = fread(buf, 1, (size_t)sz, fd);
    fclose(fd);
    if (rd != (size_t)sz) {
        ESP_LOGE(TAG, "read %u/%ld failed", (unsigned)rd, sz);
        free(buf);
        return ESP_FAIL;
    }

    micro_decoder::DecoderConfig cfg;
    cfg.ring_buffer_size       = 16 * 1024;
    cfg.transfer_buffer_size   = 8 * 1024;
    cfg.decoder_stack_in_psram = false;

    Mp3Sink sink;
    micro_decoder::DecoderSource decoder(cfg);
    decoder.set_listener(&sink);

    if (!decoder.play_buffer(buf, (size_t)sz, micro_decoder::AudioFileType::MP3)) {
        ESP_LOGE(TAG, "play_buffer failed");
        free(buf);
        return ESP_FAIL;
    }

    const TickType_t stage1_timeout = pdMS_TO_TICKS(2000);
    TickType_t start = xTaskGetTickCount();
    while (decoder.state() != micro_decoder::DecoderState::PLAYING) {
        if (decoder.state() == micro_decoder::DecoderState::FAILED) break;
        decoder.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
        if ((xTaskGetTickCount() - start) > stage1_timeout) {
            ESP_LOGW(TAG, "[mp3] timeout waiting PLAYING");
            break;
        }
    }

    while (decoder.state() == micro_decoder::DecoderState::PLAYING) {
        decoder.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
        if (decoder.state() == micro_decoder::DecoderState::FAILED) break;
    }

    ESP_LOGI(TAG, "[mp3] %s done, state=%d", path, (int)decoder.state());
    free(buf);

    if (sink.write_failed) {
        ESP_LOGE(TAG, "[mp3] write failed during playback");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ===== 按扩展名分派 ===== */
static esp_err_t PlayerPlayOnce(const char *filename)
{
    char path[160];
    snprintf(path, sizeof(path), "%s%s", LITTLEFS_MOUNT_POINT, filename);

    if (HasExt(path, ".mp3")) {
        return PlayerPlayMp3(path);
    }
    return PlayerPlayPcm(path);
}

/* ===== 同步播放（Web 上传校验用） ===== */
extern "C" esp_err_t PlayerPlayFileSync(const char *filename)
{
    if (!s_ready || filename == NULL || filename[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = PlayerPlayOnce(filename);
    ESP_LOGI(TAG, "[sync] %s -> %s", filename, esp_err_to_name(ret));
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

        /* 关闭 PA */
        Es8311PaOff();

        s_playing = false;
        s_current_owner = PLAYER_OWNER_NONE;

        /* 队列里还有东西，自己 give 一次信号量继续播 */
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
    /* 无条件插到队头 */
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

    /* CHIRP 去重：队列里已有相同 (src, is_key) 就不重复入队 */
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
        /* 直接放到队头并唤醒 */
        PlayerQueuePushHead(name, owner, src, is_key);
    } else {
        switch (owner) {
            case PLAYER_OWNER_CHIRP:
                PlayerQueuePushHead(name, owner, src, is_key);
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
    xSemaphoreGive(s_play_sem);

    ESP_LOGI(TAG, "[queue] enqueue %s (owner=%d, src=0x%04X, key=%u, idle=%d)",
             name, (int)owner, src, is_key, (int)idle);
    return true;
}

extern "C" bool PlayerIsReady(void)
{
    return s_ready;
}