#ifndef _BSP_PLAYER_H_
#define _BSP_PLAYER_H_

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 默认音频文件（相对于 LittleFS 挂载点） */
#define PLAYER_DEFAULT_FILE     "/default.mp3"

/* 文件名最大长度 */
#define PLAYER_FILENAME_MAX     64

/* MP3 文件大小上限（字节） */
#define PLAYER_MP3_MAX_SIZE     (200 * 1024)

/* 播放队列深度 */
#define PLAYER_QUEUE_DEPTH      8

/* 播放来源（优先级：CHIRP > WEB > KEY，均不打断当前播放，只影响入队位置） */
typedef enum {
    PLAYER_OWNER_NONE  = 0,
    PLAYER_OWNER_KEY   = 1,
    PLAYER_OWNER_WEB   = 2,
    PLAYER_OWNER_CHIRP = 3,
} PlayerOwner_t;

esp_err_t PlayerInit(void);

/* 旧接口：无条件插到队头（高优先级），filename == NULL 表示播默认文件 */
void PlayerTrigger(const char *filename);

/* 统一入口：按 owner 决定插队位置；CHIRP 来源按 (src, is_key) 去重。
 * is_key 语义（仅 CHIRP）：
 *   0 = PIR
 *   1 = 按键
 *   2 = 满电提醒
 *   3 = 低电量提醒
 *   4 = 离线提醒
 * 返回 true 表示已接受（直接播或入队），false 表示参数错误。
 */
bool PlayerTryPlay(const char *filename, PlayerOwner_t owner,
                   uint16_t src, uint8_t is_key);

bool PlayerIsReady(void);

/* 同步播放指定文件，等待结束再返回。仅供 Web 上传校验用。 */
esp_err_t PlayerPlayFileSync(const char *filename);

#ifdef __cplusplus
}
#endif

#endif