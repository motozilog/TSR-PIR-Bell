#ifndef _BSP_CHIRP_H_
#define _BSP_CHIRP_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* ===== E29-400T22S (ChirpIOT) 引脚定义 ===== */
#define CHIRP_UART_NUM        UART_NUM_1
#define CHIRP_UART_TX_PIN     GPIO_NUM_17
#define CHIRP_UART_RX_PIN     GPIO_NUM_18
#define CHIRP_M0_PIN          GPIO_NUM_9
#define CHIRP_M1_PIN          GPIO_NUM_8
#define CHIRP_AUX_PIN         GPIO_NUM_7

/* ===== 串口参数 ===== */
#define CHIRP_BAUD_RATE       9600            // 配置模式固定 9600 8N1

/* ===== 默认目标参数 ===== */
#define CHIRP_TARGET_ADDR     0x0001          // 默认目标地址
#define CHIRP_TARGET_NETID    0x00            // 默认目标 NetID
#define CHIRP_TARGET_CHANNEL  0x17            // 默认目标信道

/* ===== 帧格式 =====
 * [LEN] [DATA...] [CHECKSUM]
 *   LEN      = DATA 的字节数（1 ~ 254）
 *   CHECKSUM = (LEN + DATA[0] + ... + DATA[LEN-1]) & 0xFF
 * 总帧长 = LEN + 2
 */
#define CHIRP_FRAME_MAX_DATA  254
#define CHIRP_FRAME_MAX_TOTAL (CHIRP_FRAME_MAX_DATA + 2)

/* ===== 内部辅助函数 ===== */
bool ChirpWaitAuxIdle(uint32_t timeout_ms);
bool ChirpSetMode(int m0, int m1);
bool ChirpReadChannelReg(uint8_t *channel);
bool ChirpWriteReg(uint8_t reg, uint8_t val);
uint8_t ChirpSetPower(uint8_t power);
uint8_t ChirpEnableFixedPoint(void);
void ChirpChannelTask(void *args);
void ChirpRxTask(void *args);

/* ===== 对外接口 ===== */
esp_err_t ChirpInit(void);
uint8_t   ChirpGetChannel(void);
bool      ChirpIsReady(void);

/**
 * @函数说明        通过 Chirp 模块发送一帧数据（变长、指定目标）
 * @传入参数        addr   目标地址（2 字节，大端发送）
 *                   netid  目标网络 ID（1 字节）
 *                   channel 目标信道（1 字节）
 *                   data   数据内容
 *                   len    数据长度（1 ~ 254）
 * @函数返回        true=成功  false=参数错误或发送失败
 *
 * 注意：定点模式下，模块会自动在数据前加 [地址高][地址低][信道]，
 *       但 NetID 是模块级配置，需要在发送前通过配置模式写入，
 *       或者在模块里通过"定点传输"的扩展格式指定。
 */
bool ChirpSendFrame(uint16_t addr, uint8_t netid, uint8_t channel,
                    const uint8_t *data, uint8_t len);

bool ChirpSetAddress(uint16_t addr, uint8_t netid);
                    
#endif