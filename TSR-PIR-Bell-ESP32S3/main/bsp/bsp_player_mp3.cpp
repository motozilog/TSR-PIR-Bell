/**
 * @file bsp_player_mp3.cpp
 * @brief MP3 解码播放（不依赖队列，供 PlayerPlayOnce 分派调用）
 *
 * 判定成功条件（必须全满足）：
 *   1. 进入过 PLAYING 状态
 *   2. 最终状态不是 FAILED
 *   3. 实际写出过音频数据（bytes_written > 0）
 *   4. 未发生 write_failed
 */
#include "bsp_player_internal.h"
#include "bsp_player.h"
#include "bsp_es8311.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "micro_decoder/decoder_source.h"
#include "micro_decoder/types.h"

static const char *TAG = "BSP_PLAYER_MP3";

/* 单声道补双声道用的临时缓冲，放文件级避免占任务栈 */
static uint8_t s_stereo_buf[4096];

struct Mp3Sink : micro_decoder::DecoderListener {
    uint8_t  channels = 0;
    uint8_t  bits = 16;
    volatile bool write_failed = false;
    volatile size_t bytes_written = 0;

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
                    ESP_LOGE(TAG, "Es8311PlayPcm failed: %s", esp_err_to_name(err));
                    write_failed = true;
                    return 0;
                }
                bytes_written += n * 4;
                done += n;
            }
            return length;
        }

        esp_err_t err = Es8311PlayPcm(data, length);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Es8311PlayPcm failed: %s", esp_err_to_name(err));
            write_failed = true;
            return 0;
        }
        bytes_written += length;
        return length;
    }

    void on_stream_info(const micro_decoder::AudioStreamInfo &info) override {
        channels = info.get_channels();
        bits = info.get_bits_per_sample();
        ESP_LOGI(TAG, "stream: %lu Hz, %u ch, %u bit",
                 (unsigned long)info.get_sample_rate(),
                 (unsigned)channels, (unsigned)bits);
    }

    void on_state_change(micro_decoder::DecoderState state) override {
        if (state == micro_decoder::DecoderState::FAILED) {
            ESP_LOGE(TAG, "state -> FAILED");
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

extern "C" esp_err_t PlayerPlayMp3Internal(const char *path)
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
            ESP_LOGW(TAG, "rate %lu out of range, fallback 44.1k",
                     (unsigned long)rate);
            rate = 44100;
            ch   = 2;
        }
        if (Es8311PlayConfigure(rate, ch, 16) != ESP_OK) {
            ESP_LOGW(TAG, "configure(%lu,%u) failed, fallback 44.1k/2",
                     (unsigned long)rate, ch);
            Es8311PlayConfigure(44100, 2, 16);
            rate = 44100;
            ch   = 2;
        }
        ESP_LOGI(TAG, "file params: %lu Hz, %u ch",
                 (unsigned long)rate, ch);
    } else {
        ESP_LOGW(TAG, "invalid header, fallback 44.1k/2 (file may be bad)");
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

    /* ===== 阶段 1：等待进入 PLAYING ===== */
    const TickType_t stage1_timeout = pdMS_TO_TICKS(2000);
    TickType_t start = xTaskGetTickCount();
    bool entered_playing = false;

    while (decoder.state() != micro_decoder::DecoderState::PLAYING) {
        if (decoder.state() == micro_decoder::DecoderState::FAILED) break;
        decoder.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
        if ((xTaskGetTickCount() - start) > stage1_timeout) {
            ESP_LOGW(TAG, "timeout waiting PLAYING (state=%d)",
                     (int)decoder.state());
            break;
        }
    }

    /* ===== 阶段 2：播放到自然结束 ===== */
    if (decoder.state() == micro_decoder::DecoderState::PLAYING) {
        entered_playing = true;
        while (decoder.state() == micro_decoder::DecoderState::PLAYING) {
            decoder.loop();
            vTaskDelay(pdMS_TO_TICKS(10));
            if (decoder.state() == micro_decoder::DecoderState::FAILED) break;
        }
    }

    int final_state = (int)decoder.state();
    ESP_LOGI(TAG, "%s done, state=%d, entered_playing=%d, bytes=%u",
             path, final_state, (int)entered_playing, (unsigned)sink.bytes_written);
    free(buf);

    if (sink.write_failed) {
        ESP_LOGE(TAG, "write failed during playback");
        return ESP_FAIL;
    }
    if (!entered_playing) {
        ESP_LOGE(TAG, "never entered PLAYING, treat as invalid (not real MP3?)");
        return ESP_FAIL;
    }
    if (decoder.state() == micro_decoder::DecoderState::FAILED) {
        ESP_LOGE(TAG, "decoder ended in FAILED state");
        return ESP_FAIL;
    }
    if (sink.bytes_written == 0) {
        ESP_LOGE(TAG, "decoded 0 bytes of audio, treat as invalid");
        return ESP_FAIL;
    }

    return ESP_OK;
}