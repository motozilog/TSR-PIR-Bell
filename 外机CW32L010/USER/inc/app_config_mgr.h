/**
 * @file app_config_mgr.h
 * @brief 系统配置管理：内存中的全局配置读写
 */
#ifndef __APP_CONFIG_MGR_H__
#define __APP_CONFIG_MGR_H__

#include "app_config_struct.h"

/**
 * @brief 启动时用默认值初始化内存中的全局配置
 */
void AppConfig_Init(void);

/**
 * @brief 获取全局配置指针（可读写）
 * @return 指向全局 SysConfig_t 的指针
 */
SysConfig_t* AppConfig_Get(void);

/**
 * @brief 计算配置的校验和（前 28 字节累加和 & 0xFF）
 * @param cfg 配置指针
 * @return 校验和
 */
uint8_t AppConfig_CalcChecksum(const SysConfig_t *cfg);

/**
 * @brief 验证配置校验和是否正确
 * @param cfg 配置指针
 * @return 1=正确，0=错误
 */
uint8_t AppConfig_VerifyChecksum(const SysConfig_t *cfg);


/**
 * @brief 从 Flash 最后一页加载配置
 *        校验和通过 → 填入 s_config 并返回 1
 *        校验失败   → 返回 0（s_config 不变）
 * @return 1=成功，0=失败
 */
uint8_t AppConfig_LoadFromFlash(void);

/**
 * @brief 保存配置到 Flash 最后一页
 * @return 1=成功，0=失败
 */
uint8_t AppConfig_SaveToFlash(void);
#endif /* __APP_CONFIG_MGR_H__ */