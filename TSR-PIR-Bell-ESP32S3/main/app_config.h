#ifndef _APP_CONFIG_H_
#define _APP_CONFIG_H_

#include "esp_err.h"
#include <stdint.h>

/* ===== 全局配置 ===== */

/* 开发者模式：
 *   0 = 生产模式（默认，发射功率保持出厂默认 22dBm）
 *   1 = 开发模式（发射功率设为最低 14dBm，即 power=3）
 */
#define APP_DEV_MODE        1

/* ===== 系统配置结构体 ===== */
typedef struct {
    /* ChirpIoT */
    uint16_t local_addr;
    uint8_t  netid;
    uint8_t  tx_power;
    uint8_t  channel;
    uint16_t peer_addr;
    uint8_t  xtea_key[16];

    /* 开机音频是否播放 */
    uint8_t  boot_play;      /* 1=启动时播放 default.mp3，0=不播 */

    /* 音量 10~100 */
    uint8_t  volume;

    /* 正常时是否亮白灯 */
    uint8_t  led_normal_on;  /* 1=亮白，0=不亮 */

    /* 指示灯亮度 10~100，默认 50 */
    uint8_t  led_brightness;

    /* 登录密码（1~16 字符，可打印 ASCII，不含空格） */
    char     login_password[17];

    /* WiFi */
    uint8_t  wifi_mode;      /* 0=ap, 1=station */
    char     ssid[64];
    char     password[64];
    uint8_t  ip_mode;        /* 0=dhcp, 1=static */
    char     ip[16];
    char     netmask[16];
    char     gateway[16];
    char     dns1[16];
    char     dns2[16];
    char     ntp_server[64]; /* NTP 服务器，默认 ntp.aliyun.com */

    /* 门铃 4 分机 */
    struct {
        uint8_t enabled;          /* 1=启用，0=不启用（默认） */
        uint8_t mode;             /* 0=welcome, 1=doorbell, 2=alarm */
        uint8_t lowpwr_enabled;   /* 1=低电量提醒开，0=关（默认 1） */
        uint8_t offline_enabled;  /* 1=离线提醒开，0=关（默认 1） */
        uint8_t fullpwr_enabled;  /* 1=满电提醒开，0=关（默认 1） */
        uint8_t welcome_ir_audio;
        uint8_t welcome_key_audio;
        uint8_t doorbell_ir_audio;
        uint8_t doorbell_key_audio;
        uint8_t alarm_ir_audio;
        uint8_t alarm_key_audio;
    } doorbells[4];

    /* 音频备注 10 条 */
    char audio_notes[10][16];
} SysConfig_t;

/* ===== 接口 ===== */

void AppConfig_Init(void);
SysConfig_t* AppConfig_Get(void);
esp_err_t AppConfig_Save(void);
esp_err_t AppConfig_Load(void);
void AppConfig_ResetToDefault(void);

#endif /* _APP_CONFIG_H_ */