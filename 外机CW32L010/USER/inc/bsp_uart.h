/**
 * @file bsp_uart.h
 * @brief 调试串口 UART2 (DAPLink) 驱动
 */
#ifndef __BSP_UART_H__
#define __BSP_UART_H__

#include "main.h"
#include "app_chirp.h"

/* ===== 调试串口 UART2：PB05(TX) / PB06(RX)，115200 8N1 ===== */
#define DEBUG_UARTx                    CW_UART2
#define DEBUG_UART_CLK                 SYSCTRL_APB1_PERIPH_UART2
#define DEBUG_UART_APBClkENx           SYSCTRL_APBPeriphClk_Enable1
#define DEBUG_UART_BaudRate            115200
#define DEBUG_UART_UclkFreq            3000000

#define DEBUG_UART_TX_PORT             CW_GPIOB
#define DEBUG_UART_TX_PIN              GPIO_PIN_5
#define DEBUG_UART_RX_PORT             CW_GPIOB
#define DEBUG_UART_RX_PIN              GPIO_PIN_6

#define DEBUG_UART_AFTX                PB05_AFx_UART2TXD()
#define DEBUG_UART_AFRX                PB06_AFx_UART2RXD()

void Debug_UART_Init(void);
void Debug_UART_Poll(void);
void Debug_UART_SleepEnable(void);
void Debug_UART_WakeupRestore(void);
void Debug_UART_Close(void);
void Debug_UART_RxPush(uint8_t byte);

#endif /* __BSP_UART_H__ */