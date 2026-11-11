#ifndef __APP_CONFIG_STRUCT_H__
#define __APP_CONFIG_STRUCT_H__

#include <stdint.h>
#include "app_config.h"  /* 引入地址宏定义 */

/* ===== 系统配置结构体 =====
 *
 * 布局（CW32 小端，自然对齐）：
 *   偏移  长度  字段
 *   0     2     local_addr  (uint16_t)
 *   2     1     netid       (uint8_t)
 *   3     1     tx_power    (uint8_t)
 *   4     1     channel     (uint8_t)
 *   5~7   3     padding (0x00)
 *   8     2     peer_addr   (uint16_t)
 *   10~11 2     padding (0x00)
 *   12    16    xtea_key    (uint8_t[16])
 *   28    1     checksum    (uint8_t)
 *   29~31 3     padding (0x00)
 *   总长 32 字节
 *
 * checksum = 前 28 字节（偏移 0~27）的累加和 & 0xFF
 * padding 字节固定写 0x00（写前 memset）
 * 所有多字节字段按 CW32 本机字节序（小端）
 */
typedef struct {
    uint16_t local_addr;        /* 本机（CW32）ChirpIoT 地址 */
    uint8_t  netid;             /* ChirpIoT NetID */
    uint8_t  tx_power;          /* 发射功率 0~3 */
    uint8_t  channel;           /* ChirpIoT 信道 */
    uint16_t peer_addr;         /* 室内机（ESP32）ChirpIoT 地址 */
    uint8_t  xtea_key[16];      /* XTEA-128 密钥 */
    uint8_t  checksum;          /* 前 28 字节累加和 */
} SysConfig_t;

/* ===== 默认值 ===== */
/* 地址从 app_config.h 读取 */
#define SYS_CONFIG_DEFAULT_LOCAL_ADDR   (((uint16_t)APP_CHIRP_LOCAL_ADDR_H << 8) | APP_CHIRP_LOCAL_ADDR_L)
#define SYS_CONFIG_DEFAULT_NETID        0x00
#define SYS_CONFIG_DEFAULT_TX_POWER     0x00
#define SYS_CONFIG_DEFAULT_CHANNEL      0x17

/* ESP32 地址 */
#define SYS_CONFIG_DEFAULT_PEER_ADDR    0x0000

/* XTEA 密钥：20 26 20 26 20 26 20 26 20 26 20 26 20 26 20 26 */
#define SYS_CONFIG_DEFAULT_XTEA_KEY     {0x20,0x26,0x20,0x26,0x20,0x26,0x20,0x26, \
                                         0x20,0x26,0x20,0x26,0x20,0x26,0x20,0x26}

#endif /* __APP_CONFIG_STRUCT_H__ */