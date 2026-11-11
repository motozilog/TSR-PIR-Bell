/**
 * @file bsp_chirp.h
 * @brief CW32L010 ChirpIoT 模块 BSP 层
 */
#ifndef __BSP_CHIRP_H__
#define __BSP_CHIRP_H__

#include "main.h"
#include <stdbool.h>

/* ===== ChirpIoT UART1：PB01(TX) / PB00(RX)，9600 8N1 ===== */
#define CHIRP_UARTx                    CW_UART1
#define CHIRP_UART_CLK                 SYSCTRL_APB1_PERIPH_UART1
#define CHIRP_UART_APBClkENx           SYSCTRL_APBPeriphClk_Enable1
#define CHIRP_UART_BaudRate            9600
#define CHIRP_UART_UclkFreq            3000000

#define CHIRP_TX_PORT                  CW_GPIOB
#define CHIRP_TX_PIN                   GPIO_PIN_1
#define CHIRP_RX_PORT                  CW_GPIOB
#define CHIRP_RX_PIN                   GPIO_PIN_0

#define CHIRP_TX_AF                    PB01_AFx_UART1TXD()
#define CHIRP_RX_AF                    PB00_AFx_UART1RXD()

/* ===== M0 / M1 / AUX ===== */
#define CHIRP_M0_PORT                  CW_GPIOA
#define CHIRP_M0_PIN                   GPIO_PIN_3
#define CHIRP_M1_PORT                  CW_GPIOA
#define CHIRP_M1_PIN                   GPIO_PIN_4
#define CHIRP_AUX_PORT                 CW_GPIOA
#define CHIRP_AUX_PIN                  GPIO_PIN_5

#define CHIRP_M0_LOW()                 GPIO_WritePin(CHIRP_M0_PORT, CHIRP_M0_PIN, GPIO_Pin_RESET)
#define CHIRP_M0_HIGH()                GPIO_WritePin(CHIRP_M0_PORT, CHIRP_M0_PIN, GPIO_Pin_SET)
#define CHIRP_M1_LOW()                 GPIO_WritePin(CHIRP_M1_PORT, CHIRP_M1_PIN, GPIO_Pin_RESET)
#define CHIRP_M1_HIGH()                GPIO_WritePin(CHIRP_M1_PORT, CHIRP_M1_PIN, GPIO_Pin_SET)
#define CHIRP_AUX_READ()               GPIO_ReadPin(CHIRP_AUX_PORT, CHIRP_AUX_PIN)

/* ===== FIFO 尺寸 ===== */
#define CHIRP_RX_FIFO_SIZE             256
#define CHIRP_TX_FIFO_SIZE             256
#define CHIRP_FRAME_MAX_DATA           64

void Chirp_Init(void);
uint8_t Chirp_ConfigInit(void);
void Chirp_SendByte(uint8_t byte);
bool Chirp_SendFrame(uint16_t addr, uint8_t netid, uint8_t channel,
                     const uint8_t *data, uint8_t len);
uint16_t Chirp_GetPacket(uint8_t *buf, uint16_t max_len);
void Chirp_FlushRxBuffer(void);
void Chirp_WaitTxIdle(void);   /* 等 TX FIFO 发空（休眠前用） */
uint8_t Chirp_SetWorDelay(uint16_t ms);

uint8_t Chirp_ReadChannel(uint8_t *channel);
uint8_t Chirp_SetAddress(uint16_t addr, uint8_t netid);
uint8_t Chirp_EnableFixedPoint(void);
uint8_t Chirp_SetPower(uint8_t power);
uint8_t Chirp_SetWorPeriod(uint16_t period_ms);   /* 配 WOR 监听周期 */
uint8_t Chirp_SetWorDelay(uint16_t delay_ms);     /* 配 WOR 唤醒后延时 */
void    Chirp_EnterWorMode(void);                 /* 进 WOR 模式（M0=1,M1=0） */


void Chirp_EnterConfigMode(void);
void Chirp_EnterTransparentMode(void);
void Chirp_EnterReceiveMode(void);
void Chirp_EnterDefaultMode(void);

void Chirp_SleepEnable(void);
void Chirp_WakeupRestore(void);
void Chirp_GpioSleepEnable(void);

/* FIFO 内部接口（中断里用） */
void Chirp_RxFifoInit(void);
void Chirp_RxFifoPush(uint8_t byte);
uint16_t Chirp_RxFifoCount(void);
uint16_t Chirp_RxFifoRead(uint8_t *buf, uint16_t max_len);
void Chirp_RxFifoFlush(void);
uint32_t Chirp_RxOverflowCount(void);

void Chirp_TxFifoInit(void);
uint16_t Chirp_TxFifoCount(void);
uint16_t Chirp_TxFifoFree(void);
bool Chirp_TxFifoPop(uint8_t *byte);
uint32_t Chirp_TxOverflowCount(void);

#endif /* __BSP_CHIRP_H__ */