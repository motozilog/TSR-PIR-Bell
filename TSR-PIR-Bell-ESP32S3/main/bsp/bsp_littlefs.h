#ifndef _BSP_LITTLEFS_H_
#define _BSP_LITTLEFS_H_

#include "esp_err.h"

// LittleFS 挂载点
#define LITTLEFS_MOUNT_POINT    "/littlefs"

// 对外接口
esp_err_t LittleFsInit(void);

#endif