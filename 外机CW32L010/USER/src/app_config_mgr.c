/**
 * @file app_config_mgr.c
 * @brief 系统配置管理实现
 */
#include "app_config_mgr.h"
#include "cw32l010_flash.h"
#include <string.h>
#include <stdlib.h>

/* 配置存储：Flash 最后一页（Page 127） */
//#define CONFIG_FLASH_PAGE       127
#define CONFIG_FLASH_ADDR       (512 * 127)   /* 0xFE00 */
#define CONFIG_FLASH_SIZE       512

/* 全局配置实例（封装，外部通过 AppConfig_Get() 访问） */
static SysConfig_t s_config;

/******************************************************************************
 * 对外接口
 ******************************************************************************/

void AppConfig_Init(void)
{
    /* 1. 全部清零（padding 也是 0） */
    memset(&s_config, 0, sizeof(SysConfig_t));

    /* 2. 填默认值 */
    s_config.local_addr = SYS_CONFIG_DEFAULT_LOCAL_ADDR;
    s_config.netid      = SYS_CONFIG_DEFAULT_NETID;
    s_config.tx_power   = SYS_CONFIG_DEFAULT_TX_POWER;
    s_config.channel    = SYS_CONFIG_DEFAULT_CHANNEL;
    s_config.peer_addr  = SYS_CONFIG_DEFAULT_PEER_ADDR;

    /* 3. XTEA 密钥 */
    {
        const uint8_t default_key[16] = SYS_CONFIG_DEFAULT_XTEA_KEY;
        memcpy(s_config.xtea_key, default_key, 16);
    }

    /* 4. 算校验和 */
    s_config.checksum = AppConfig_CalcChecksum(&s_config);
}

SysConfig_t* AppConfig_Get(void)
{
    return &s_config;
}

uint8_t AppConfig_CalcChecksum(const SysConfig_t *cfg)
{
    const uint8_t *p = (const uint8_t *)cfg;
    uint32_t sum = 0;
    size_t  i;

    for (i = 0; i < offsetof(SysConfig_t, checksum); i++)
    {
        sum += p[i];
    }

    return (uint8_t)(sum & 0xFF);
}

uint8_t AppConfig_VerifyChecksum(const SysConfig_t *cfg)
{
    return (AppConfig_CalcChecksum(cfg) == cfg->checksum) ? 1 : 0;
}

/******************************************************************************
 * Flash 读写
 ******************************************************************************/

uint8_t AppConfig_LoadFromFlash(void)
{
    SysConfig_t tmp;
    uint8_t *p = (uint8_t *)&tmp;
    uint16_t i;

    /* 1. 从 Flash 读 32 字节（直接按地址读，Flash 已映射） */
    for (i = 0; i < sizeof(SysConfig_t); i++)
    {
        p[i] = *((volatile uint8_t *)(CONFIG_FLASH_ADDR + i));
				printf("%02X ", p[i]);
    }

    /* 2. 校验和验证 */
    if (AppConfig_VerifyChecksum(&tmp) == 0)
    {
        return 0;   /* 无效 */
    }

    /* 3. 有效 → 填回全局配置 */
    memcpy(&s_config, &tmp, sizeof(SysConfig_t));
    return 1;
}

uint8_t AppConfig_SaveToFlash(void)
{
    uint8_t  err;
    uint8_t  Flag;
    uint16_t i;
    uint8_t  all_ff = 1;

    s_config.checksum = AppConfig_CalcChecksum(&s_config);

    /* 解锁 Page 80（官方例程要求） */
    FLASH_UnlockPage(80);
    Flag = CW_FLASH->ISR;

    /* 解锁最后一页 */
    FLASH_UnlockPages(CONFIG_FLASH_ADDR, CONFIG_FLASH_ADDR);

    /* 擦除 */
    Flag = FLASH_ErasePages(CONFIG_FLASH_ADDR, CONFIG_FLASH_ADDR);
    printf("Erase return = 0x%02X (0=OK)\r\n", Flag);
    if (Flag != 0)
    {
        printf("!!! Erase FAILED\r\n");
        return 0;
    }

    /* Blank check 第一遍：只判断是否全 0xFF，不缓存 */
    for (i = 0; i < CONFIG_FLASH_SIZE; i++)
    {
        if (*((volatile uint8_t *)(CONFIG_FLASH_ADDR + i)) != 0xFF)
        {
            all_ff = 0;
            break;
        }
    }

    /* 只有非全 0xFF 才第二遍逐字节打印 */
    if (!all_ff)
    {
        printf("Blank check: ");
        for (i = 0; i < CONFIG_FLASH_SIZE; i++)
        {
            printf("%02X ", *((volatile uint8_t *)(CONFIG_FLASH_ADDR + i)));
        }
        printf("\r\n");
    }

    /* 不 Lock，直接写（关键：擦除后不 Lock） */
    err = FLASH_WriteBytes(CONFIG_FLASH_ADDR, (uint8_t *)&s_config, sizeof(SysConfig_t));
    //FLASH_LockAllPages();
    if (err != 0)
    {
        return 0;
    }

    return 1;
}


