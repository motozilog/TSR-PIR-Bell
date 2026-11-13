/**
 * @file bsp_player_internal.h
 * @brief 播放器内部共享声明（不对外暴露）
 *
 * 拆分后各文件职责：
 *   bsp_player.cpp     队列 / 播放任务 / 对外接口 / 分派 / 同步异步校验
 *   bsp_player_pcm.cpp PCM 直读播放
 *   bsp_player_mp3.cpp MP3 解码播放
 */
#ifndef _BSP_PLAYER_INTERNAL_H_
#define _BSP_PLAYER_INTERNAL_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PCM 直读播放：path 为完整路径（含挂载点） */
esp_err_t PlayerPlayPcmInternal(const char *path);

/* MP3 解码播放：path 为完整路径（含挂载点） */
esp_err_t PlayerPlayMp3Internal(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* _BSP_PLAYER_INTERNAL_H_ */