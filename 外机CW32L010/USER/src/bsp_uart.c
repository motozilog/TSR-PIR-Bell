/**
 * @file bsp_uart.c
 * @brief 调试串口 UART2 (DAPLink) 实现 + printf 重定向 + 接收回显
 */
#include "bsp_uart.h"
#include "app_chirp.h"
#include "bsp_systick.h"

/* ===== 接收 FIFO（64 字节） ===== */
#define DEBUG_UART_RX_FIFO_SIZE   64

/* ===== 协议组帧缓冲 ===== */
#define DEBUG_FRAME_BUF_SIZE   APP_MSG_FRAME_MAX
static uint8_t  s_frame_buf[DEBUG_FRAME_BUF_SIZE];
static uint16_t s_frame_len   = 0;
static uint32_t s_last_rx_tick = 0;
#define DEBUG_FRAME_TIMEOUT_MS  100

static volatile uint8_t  s_rx_fifo[DEBUG_UART_RX_FIFO_SIZE];
static volatile uint16_t s_rx_head = 0;   /* 中断写 */
static volatile uint16_t s_rx_tail = 0;   /* 主循环读 */

/******************************************************************************
 * 接收 FIFO 内部函数
 ******************************************************************************/
void Debug_UART_RxPush(uint8_t byte)
{
    uint16_t next = (uint16_t)((s_rx_head + 1) % DEBUG_UART_RX_FIFO_SIZE);
    if (next != s_rx_tail)
    {
        s_rx_fifo[s_rx_head] = byte;
        s_rx_head = next;
    }
    /* FIFO 满则丢弃（调试串口，不做溢出计数） */
}

static uint8_t Debug_UART_RxPop(uint8_t *byte)
{
    if (s_rx_tail == s_rx_head) return 0;   /* 空 */
    *byte = s_rx_fifo[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1) % DEBUG_UART_RX_FIFO_SIZE);
    return 1;
}

/******************************************************************************
 * 初始化
 ******************************************************************************/
void Debug_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    UART_InitTypeDef UART_InitStructure = {0};

    /* TX PB05 */
    GPIO_WritePin(DEBUG_UART_TX_PORT, DEBUG_UART_TX_PIN, GPIO_Pin_SET);
    GPIO_InitStructure.Pins = DEBUG_UART_TX_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_Init(DEBUG_UART_TX_PORT, &GPIO_InitStructure);

    /* RX PB06 */
    GPIO_InitStructure.Pins = DEBUG_UART_RX_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT_PULLUP;
    GPIO_Init(DEBUG_UART_RX_PORT, &GPIO_InitStructure);

    /* AF 复用 */
    DEBUG_UART_AFTX;
    DEBUG_UART_AFRX;

    /* UART2 配置 */
    UART_InitStructure.UART_BaudRate            = DEBUG_UART_BaudRate;
    UART_InitStructure.UART_Over                = UART_Over_16;
    UART_InitStructure.UART_Source              = UART_Source_PCLK;
    UART_InitStructure.UART_UclkFreq            = DEBUG_UART_UclkFreq;
    UART_InitStructure.UART_StartBit            = UART_StartBit_FE;
    UART_InitStructure.UART_StopBits            = UART_StopBits_1;
    UART_InitStructure.UART_Parity              = UART_Parity_No;
    UART_InitStructure.UART_HardwareFlowControl = UART_HardwareFlowControl_None;
    UART_InitStructure.UART_Mode                = UART_Mode_Rx | UART_Mode_Tx;
    UART_Init(DEBUG_UARTx, &UART_InitStructure);

    /* 接收中断 + FIFO */
    s_rx_head = 0;
    s_rx_tail = 0;
    s_frame_len = 0;
    UART_ITConfig(DEBUG_UARTx, UART_IT_RC, ENABLE);
    NVIC_SetPriority(UART2_IRQn, 3);
    NVIC_EnableIRQ(UART2_IRQn);
}

void Debug_UART_Poll(void)
{
    uint8_t byte;

    while (Debug_UART_RxPop(&byte))
    {
        s_last_rx_tick = GetTick();

        /* 首字节 = LEN（payload 长度） */
        if (s_frame_len == 0)
        {
            if (byte > APP_MSG_PAYLOAD_MAX)
            {
                /* 非法 LEN，丢弃 */
                continue;
            }
            s_frame_buf[s_frame_len++] = byte;
            continue;
        }

        /* 后续字节追加 */
        s_frame_buf[s_frame_len++] = byte;

        /* 判断是否凑齐一帧：2 + LEN */
        if (s_frame_len >= 2)
        {
            uint16_t expect = (uint16_t)(2 + s_frame_buf[0]);
            if (expect > DEBUG_FRAME_BUF_SIZE)
            {
                /* 异常，丢弃 */
                s_frame_len = 0;
                continue;
            }
            if (s_frame_len >= expect)
            {
                AppChirp_OnDebugBytes(s_frame_buf, s_frame_len);
                s_frame_len = 0;
            }
        }
    }

    /* 超时清半帧 */
    if (s_frame_len > 0 &&
        (GetTick() - s_last_rx_tick) >= DEBUG_FRAME_TIMEOUT_MS)
    {
        printf("[APP] Frame timeout, discard %u bytes\r\n", s_frame_len);
        s_frame_len = 0;
    }
}

/* 休眠前：关闭 UART2 接收中断 */
void Debug_UART_SleepEnable(void)
{
    UART_ITConfig(DEBUG_UARTx, UART_IT_RC, DISABLE);
    NVIC_DisableIRQ(UART2_IRQn);
}

/* 唤醒后：恢复 UART2 接收中断（本版唤醒后走 Debug_UART_Init 全量重来） */
void Debug_UART_WakeupRestore(void)
{
    s_rx_head = 0;
    s_rx_tail = 0;
    s_frame_len = 0;
    UART_ITConfig(DEBUG_UARTx, UART_IT_RC, ENABLE);
    NVIC_EnableIRQ(UART2_IRQn);
}

/**
 * @brief 休眠前关闭调试串口 UART2：reset 后关 UART2 时钟
 *        本版不动 GPIOB，因为 GPIOB 上还挂着 Chirp/LED/FULL/BATT
 */
void Debug_UART_Close(void)
{
    /* 复位 UART2 */
    SYSCTRL_APBPeriphReset1(SYSCTRL_APB1_PERIPH_UART2, ENABLE);
    SYSCTRL_APBPeriphReset1(SYSCTRL_APB1_PERIPH_UART2, DISABLE);

    /* 关 UART2 时钟 */
    SYSCTRL_APBPeriphClk_Enable1(SYSCTRL_APB1_PERIPH_UART2, DISABLE);
}

/******************************************************************************
 * printf 重定向
 ******************************************************************************/
int __retarget_putchar(int ch)
{
    UART_SendData_8bit(DEBUG_UARTx, (uint8_t)ch);
    while (UART_GetFlagStatus(DEBUG_UARTx, UART_FLAG_TXE) == RESET);
    return ch;
}

size_t __write(int handle, const unsigned char * buffer, size_t size)
{
    size_t nChars = 0;
    if (buffer == 0) return 0;

    for (; size != 0; --size)
    {
        UART_SendData_8bit(DEBUG_UARTx, *buffer++);
        while (UART_GetFlagStatus(DEBUG_UARTx, UART_FLAG_TXE) == RESET);
        ++nChars;
    }
    return nChars;
}