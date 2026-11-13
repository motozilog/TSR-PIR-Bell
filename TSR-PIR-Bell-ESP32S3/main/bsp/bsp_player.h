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

/* 播放队列深度（IP 播报最坏 16 片段 + 其它事件余量） */
#define PLAYER_QUEUE_DEPTH      32

/* 播放来源（优先级：CONFIG > CHIRP > WEB > KEY，均不打断当前播放，只影响入队位置）
 * CONFIG：IP 播报等"必须播完"的配置反馈音，插到队头且其它事件不得插到其前
 */
typedef enum {
    PLAYER_OWNER_NONE   = 0,
    PLAYER_OWNER_KEY    = 1,
    PLAYER_OWNER_WEB    = 2,
    PLAYER_OWNER_CHIRP  = 3,
    PLAYER_OWNER_CONFIG = 4,
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

/* 当前是否有音频正在播放（供外部串行投喂节流 / 上传忙判定） */
bool PlayerIsPlaying(void);

/* 批量入队模式：Begin 后 PlayerTryPlay 只入队不唤醒 player_task，
 * End 时若队列非空再唤醒。用于 IP 播报等需要连续入队多片段的场景，
 * 避免第一片段被 idle 分支立即抢播导致乱序。
 * 注意：不可嵌套调用。 */
void PlayerBatchBegin(void);
void PlayerBatchEnd(void);

/* 同步播放指定文件，等待结束再返回（在调用者任务栈上跑解码）。
 * 仅供非 httpd 场景用；httpd 上传校验请用 PlayerPlayFileSyncEx。 */
esp_err_t PlayerPlayFileSync(const char *filename);

/* 异步校验播放：内部创建一次性任务（栈 12KB）跑解码，调用者阻塞等结果。
 * 用于 httpd 上传校验，避免解码链在 httpd 任务栈上爆栈。
 * timeout_ms：等待超时，超时返回 ESP_ERR_TIMEOUT。 */
esp_err_t PlayerPlayFileSyncEx(const char *filename, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif