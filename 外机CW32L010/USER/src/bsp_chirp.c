/**
 * @file bsp_chirp.c
 * @author WHXY
 * @brief CW32L010 ChirpIoT 模块 BSP 层实现
 *        接收：中断 + 无锁 FIFO
 *        发送：中断 + 无锁 FIFO（数据帧），配置阶段仍阻塞式
 */

#include "bsp_chirp.h"
#include "bsp_systick.h"
#include "app_config.h"
#include "app_config_mgr.h"

/******************************************************************************
 * 接收 FIFO 内部变量
 ******************************************************************************/
static volatile uint8_t  s_rx_fifo[CHIRP_RX_FIFO_SIZE];
static volatile uint16_t s_rx_head = 0;   /* 生产者（中断）只写这个 */
static volatile uint16_t s_rx_tail = 0;   /* 消费者（主循环）只写这个 */
static volatile uint32_t s_rx_overflow = 0;

/******************************************************************************
 * 发送 FIFO 内部变量
 ******************************************************************************/
static volatile uint8_t  s_tx_fifo[CHIRP_TX_FIFO_SIZE];
static volatile uint16_t s_tx_head = 0;   /* 生产者（主循环）只写这个 */
static volatile uint16_t s_tx_tail = 0;   /* 消费者（中断）只写这个 */
static volatile uint32_t s_tx_overflow = 0;

/******************************************************************************
 * 内部函数声明
 ******************************************************************************/
static void Chirp_UART_Config(void);
static void Chirp_GPIO_Config(void);
static void Chirp_WaitTxFifoEmpty(void);
static void Chirp_TxKick(void);           /* 确保 TXE 中断已开 */

/******************************************************************************
 * 接收 FIFO 实现
 ******************************************************************************/
void Chirp_RxFifoInit(void)
{
    s_rx_head = 0;
    s_rx_tail = 0;
    s_rx_overflow = 0;
}

void Chirp_RxFifoPush(uint8_t byte)
{
    uint16_t next = (uint16_t)((s_rx_head + 1) % CHIRP_RX_FIFO_SIZE);
    if (next != s_rx_tail)
    {
        s_rx_fifo[s_rx_head] = byte;
        s_rx_head = next;
    }
    else
    {
        s_rx_overflow++;
    }
}

uint16_t Chirp_RxFifoCount(void)
{
    uint16_t h = s_rx_head;
    uint16_t t = s_rx_tail;
    if (h >= t) return (uint16_t)(h - t);
    return (uint16_t)(CHIRP_RX_FIFO_SIZE - t + h);
}

uint16_t Chirp_RxFifoRead(uint8_t *buf, uint16_t max_len)
{
    uint16_t len = 0;
    uint16_t h;

    if (buf == NULL || max_len == 0) return 0;

    h = s_rx_head;
    while (s_rx_tail != h && len < max_len)
    {
        buf[len++] = s_rx_fifo[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1) % CHIRP_RX_FIFO_SIZE);
    }
    return len;
}

void Chirp_RxFifoFlush(void)
{
    s_rx_tail = s_rx_head;
}

uint32_t Chirp_RxOverflowCount(void)
{
    return s_rx_overflow;
}

/******************************************************************************
 * 发送 FIFO 实现
 ******************************************************************************/
void Chirp_TxFifoInit(void)
{
    s_tx_head = 0;
    s_tx_tail = 0;
    s_tx_overflow = 0;
}

uint16_t Chirp_TxFifoCount(void)
{
    uint16_t h = s_tx_head;
    uint16_t t = s_tx_tail;
    if (h >= t) return (uint16_t)(h - t);
    return (uint16_t)(CHIRP_TX_FIFO_SIZE - t + h);
}

/* 剩余可插入字节数（牺牲 1 字节区分空/满） */
uint16_t Chirp_TxFifoFree(void)
{
    return (uint16_t)(CHIRP_TX_FIFO_SIZE - 1 - Chirp_TxFifoCount());
}

/* 中断里调用：从 FIFO 取 1 字节 */
bool Chirp_TxFifoPop(uint8_t *byte)
{
    if (byte == NULL) return false;
    if (s_tx_tail == s_tx_head) return false;   /* 空 */
    *byte = s_tx_fifo[s_tx_tail];
    s_tx_tail = (uint16_t)((s_tx_tail + 1) % CHIRP_TX_FIFO_SIZE);
    return true;
}

uint32_t Chirp_TxOverflowCount(void)
{
    return s_tx_overflow;
}

/* 临界区：把整帧 push 进 TX FIFO，并确保 TXE 中断已开 */
static void Chirp_TxKick(void)
{
    __disable_irq();
    UART_ITConfig(CHIRP_UARTx, UART_IT_TXE, ENABLE);
    __enable_irq();
}

/* 等 TX FIFO 完全发完（阻塞） */
static void Chirp_WaitTxFifoEmpty(void)
{
    while (Chirp_TxFifoCount() > 0)
    {
        /* 空转等待，由 TXE 中断吐出 */
    }
}

/* 导出：等 TX FIFO 发空，休眠前调用，确保最后一帧发完 */
void Chirp_WaitTxIdle(void)
{
    Chirp_WaitTxFifoEmpty();
}

/******************************************************************************
 * 初始化
 ******************************************************************************/
void Chirp_Init(void)
{
    Chirp_RxFifoInit();
    Chirp_TxFifoInit();
    Chirp_GPIO_Config();
    Chirp_UART_Config();

    /* 打开 UART1 接收完成中断（发送 TXE 中断在需要发时再打开） */
    UART_ITConfig(CHIRP_UARTx, UART_IT_RC, ENABLE);

    /* NVIC：优先级 2（SysTick=0，后续 GPIOA=1） */
    NVIC_SetPriority(UART1_IRQn, 2);
    NVIC_EnableIRQ(UART1_IRQn);
}



static void Chirp_GPIO_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.Pins = CHIRP_TX_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_Init(CHIRP_TX_PORT, &GPIO_InitStructure);
    GPIO_WritePin(CHIRP_TX_PORT, CHIRP_TX_PIN, GPIO_Pin_SET);

    GPIO_InitStructure.Pins = CHIRP_RX_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT_PULLUP;
    GPIO_Init(CHIRP_RX_PORT, &GPIO_InitStructure);

    CHIRP_TX_AF;
    CHIRP_RX_AF;

    GPIO_InitStructure.Pins = CHIRP_M0_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_Init(CHIRP_M0_PORT, &GPIO_InitStructure);
    GPIO_WritePin(CHIRP_M0_PORT, CHIRP_M0_PIN, GPIO_Pin_RESET);

    GPIO_InitStructure.Pins = CHIRP_M1_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_Init(CHIRP_M1_PORT, &GPIO_InitStructure);
    GPIO_WritePin(CHIRP_M1_PORT, CHIRP_M1_PIN, GPIO_Pin_RESET);

    /* AUX：输入上拉，非休眠态不使能中断 */
    GPIO_InitStructure.Pins = CHIRP_AUX_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT_PULLUP;
    GPIO_InitStructure.IT   = GPIO_IT_NONE;
    GPIO_Init(CHIRP_AUX_PORT, &GPIO_InitStructure);
}

static void Chirp_UART_Config(void)
{
    UART_InitTypeDef UART_InitStructure = {0};

    UART_InitStructure.UART_BaudRate            = CHIRP_UART_BaudRate;
    UART_InitStructure.UART_Over                = UART_Over_16;
    UART_InitStructure.UART_Source              = UART_Source_PCLK;
    UART_InitStructure.UART_UclkFreq            = CHIRP_UART_UclkFreq;
    UART_InitStructure.UART_StartBit            = UART_StartBit_FE;
    UART_InitStructure.UART_StopBits            = UART_StopBits_1;
    UART_InitStructure.UART_Parity              = UART_Parity_No;
    UART_InitStructure.UART_HardwareFlowControl = UART_HardwareFlowControl_None;
    UART_InitStructure.UART_Mode                = UART_Mode_Rx | UART_Mode_Tx;
    UART_Init(CHIRP_UARTx, &UART_InitStructure);
}

/******************************************************************************
 * 模式切换
 ******************************************************************************/
void Chirp_EnterConfigMode(void)
{
    CHIRP_M0_LOW();
    CHIRP_M1_HIGH();

    uint32_t start = GetTick();
    while (CHIRP_AUX_READ() == GPIO_Pin_RESET && (GetTick() - start) < 500);

    Delay_ms(200);
}

void Chirp_EnterTransparentMode(void)
{
    CHIRP_M0_LOW();
    CHIRP_M1_LOW();

    uint32_t start = GetTick();
    while (CHIRP_AUX_READ() == GPIO_Pin_RESET && (GetTick() - start) < 500);

    Delay_ms(200);
}

void Chirp_EnterReceiveMode(void)
{
    CHIRP_M0_LOW();
    CHIRP_M1_LOW();

    uint32_t start = GetTick();
    while (CHIRP_AUX_READ() == GPIO_Pin_RESET && (GetTick() - start) < 500);

    Delay_ms(200);
}

void Chirp_EnterDefaultMode(void)
{
#if APP_DEV_MODE
    CHIRP_M0_LOW();
    CHIRP_M1_LOW();
#else
    CHIRP_M0_LOW();
    CHIRP_M1_LOW();
#endif

    uint32_t start = GetTick();
    while (CHIRP_AUX_READ() == GPIO_Pin_RESET && (GetTick() - start) < 500);

    Delay_ms(10);
}

/******************************************************************************
 * 阻塞式单字节发送（仅配置阶段使用）
 ******************************************************************************/
void Chirp_SendByte(uint8_t byte)
{
    while (UART_GetFlagStatus(CHIRP_UARTx, UART_FLAG_TXE) == RESET);
    UART_SendData_8bit(CHIRP_UARTx, byte);
}

/* 清空接收 FIFO（消费者操作） */
void Chirp_FlushRxBuffer(void)
{
    Chirp_RxFifoFlush();
}

/******************************************************************************
 * 发送帧：整帧检查 + 一次性压入 TX FIFO
 ******************************************************************************/
/******************************************************************************
 * 发送帧：整帧检查 + 一次性压入 TX FIFO
 ******************************************************************************/
bool Chirp_SendFrame(uint16_t addr, uint8_t netid, uint8_t channel,
                     const uint8_t *data, uint8_t len)
{
    (void)netid;   /* netid 为模块级参数，帧内不使用 */

    if (data == NULL || len == 0 || len > CHIRP_FRAME_MAX_DATA)
    {
        printf("[CHIRP] SendFrame: invalid len=%d\r\n", len);
        return false;
    }

    /* ===== 1. 发送前确保本机地址配置正确 ===== */
    static uint16_t s_configured_local_addr = 0xFFFF; 
    SysConfig_t *cfg = AppConfig_Get();
    uint16_t target_addr = cfg->local_addr;

    if (s_configured_local_addr != target_addr)
    {
        if (Chirp_SetAddress(target_addr, cfg->netid))
        {
            s_configured_local_addr = target_addr;
            printf("[CHIRP] Ensure local addr set to 0x%04X OK\r\n", target_addr);
        }
        else
        {
            printf("[CHIRP] Ensure local addr set failed!\r\n");
        }
    }
    
    /* 因为 Chirp_SetAddress 会进配置模式，结束时已调用了 Chirp_EnterDefaultMode，
       但如果没进 if (说明地址已对)，我们仍需确保当前在传输模式 (M0=0, M1=0) */
    Chirp_EnterDefaultMode();

    /* ===== 2. 长度与空间检查 ===== */
    /* 计算总帧长：3 字节定点头 + [LEN + DATA + CHECKSUM] */
    uint16_t frame_len = (uint16_t)(3 + 1 + len + 1);

    /* 整帧检查：TX FIFO 空间不够就整帧丢弃 */
    if (Chirp_TxFifoFree() < frame_len)
    {
        s_tx_overflow++;
        printf("[CHIRP] SendFrame: TX FIFO full (need %d, free %d), drop whole frame\r\n",
               frame_len, Chirp_TxFifoFree());
        return false;
    }

    /* ===== 3. 计算校验和 ===== */
    uint8_t sum = len;
    for (uint8_t i = 0; i < len; i++) sum = (uint8_t)(sum + data[i]);

    /* ===== 4. 临界区：一次性把整帧压入 FIFO ===== */
    __disable_irq();

    /* 1. 定点传输头 */
    s_tx_fifo[s_tx_head] = (uint8_t)((addr >> 8) & 0xFF);
    s_tx_head = (uint16_t)((s_tx_head + 1) % CHIRP_TX_FIFO_SIZE);
    s_tx_fifo[s_tx_head] = (uint8_t)((addr >> 0) & 0xFF);
    s_tx_head = (uint16_t)((s_tx_head + 1) % CHIRP_TX_FIFO_SIZE);
    s_tx_fifo[s_tx_head] = channel;
    s_tx_head = (uint16_t)((s_tx_head + 1) % CHIRP_TX_FIFO_SIZE);

    /* 2. [LEN][DATA...][CHECKSUM] */
    s_tx_fifo[s_tx_head] = len;
    s_tx_head = (uint16_t)((s_tx_head + 1) % CHIRP_TX_FIFO_SIZE);

    for (uint8_t i = 0; i < len; i++)
    {
        s_tx_fifo[s_tx_head] = data[i];
        s_tx_head = (uint16_t)((s_tx_head + 1) % CHIRP_TX_FIFO_SIZE);
    }

    s_tx_fifo[s_tx_head] = sum;
    s_tx_head = (uint16_t)((s_tx_head + 1) % CHIRP_TX_FIFO_SIZE);

    /* 3. 确保 TXE 中断已开 */
    UART_ITConfig(CHIRP_UARTx, UART_IT_TXE, ENABLE);

    __enable_irq();

    /* ===== 5. 日志 ===== */
    printf("[CHIRP] TX -> addr=0x%04X netid=0x%02X ch=0x%02X len=%d data:",
           addr, netid, channel, len);
    for (uint8_t i = 0; i < len; i++) printf(" %02X", data[i]);
    printf(" sum:%02X\r\n", sum);

    return true;
}

/******************************************************************************
 * 从 FIFO 收包
 ******************************************************************************/
uint16_t Chirp_GetPacket(uint8_t *buf, uint16_t max_len)
{
    uint16_t len = 0;
    uint32_t start;

    if (buf == NULL || max_len == 0) return 0;

    /* AUX 高且 FIFO 空 → 无数据 */
    if (CHIRP_AUX_READ() != GPIO_Pin_RESET && Chirp_RxFifoCount() == 0)
    {
        return 0;
    }

    start = GetTick();
    while (len < max_len)
    {
        uint16_t got = Chirp_RxFifoRead(buf + len, (uint16_t)(max_len - len));
        len += got;

        if (CHIRP_AUX_READ() != GPIO_Pin_RESET)
        {
            uint32_t tail_start = GetTick();
            while ((GetTick() - tail_start) < 2)
            {
                if (Chirp_RxFifoCount() > 0) break;
            }
            while (len < max_len)
            {
                uint16_t g = Chirp_RxFifoRead(buf + len, (uint16_t)(max_len - len));
                if (g == 0) break;
                len += g;
            }
            break;
        }

        if ((GetTick() - start) > 500)
        {
            break;
        }
    }

    return len;
}

/******************************************************************************
 * 信道读取
 ******************************************************************************/
uint8_t Chirp_ReadChannel(uint8_t *channel)
{
    uint8_t rx_buf[8] = {0};
    uint8_t rx_cnt = 0;
    uint32_t start;
    uint8_t ret = 0;
    uint8_t i;

    Chirp_WaitTxFifoEmpty();   /* 确保之前的数据帧发完 */

    Chirp_EnterConfigMode();

    printf("[CHIRP] TX: C1 05 01\r\n");

    Chirp_RxFifoFlush();

    Chirp_SendByte(0xC1);
    Chirp_SendByte(0x05);
    Chirp_SendByte(0x01);

    while (UART_GetFlagStatus(CHIRP_UARTx, UART_FLAG_TC) == RESET);

    start = GetTick();
    while ((GetTick() - start) < 200)
    {
        uint8_t tmp;
        if (Chirp_RxFifoRead(&tmp, 1) == 1)
        {
            if (rx_cnt < sizeof(rx_buf))
                rx_buf[rx_cnt++] = tmp;
            if (rx_cnt >= 4) break;
        }
    }

    printf("[CHIRP] RX (%d bytes):", rx_cnt);
    for (i = 0; i < rx_cnt; i++) printf(" %02X", rx_buf[i]);
    printf("\r\n");

    if (rx_cnt >= 4 && rx_buf[0] == 0xC1 && rx_buf[1] == 0x05 && rx_buf[2] == 0x01)
    {
        *channel = rx_buf[3];
        ret = 1;
        printf("[CHIRP] Channel = %d\r\n", *channel);
    }
    else
    {
        printf("[CHIRP] Parse failed (expect C1 05 01 XX)\r\n");
    }

    Chirp_EnterDefaultMode();

    Delay_ms(5);
    return ret;
}

/******************************************************************************
 * 地址配置（只写地址 + NetID）
 ******************************************************************************/
uint8_t Chirp_SetAddress(uint16_t addr, uint8_t netid)
{
    uint8_t rx_buf[16] = {0};
    uint8_t rx_cnt = 0;
    uint32_t start;
    uint8_t ret = 0;
    uint8_t i;
    uint8_t addr_h = (uint8_t)(addr >> 8);
    uint8_t addr_l = (uint8_t)(addr & 0xFF);

    Chirp_WaitTxFifoEmpty();

    Chirp_EnterConfigMode();

    printf("[CHIRP] Set Addr=0x%04X, NetID=0x%02X\r\n", addr, netid);

    Chirp_RxFifoFlush();

    Chirp_SendByte(0xC0);
    Chirp_SendByte(0x00);
    Chirp_SendByte(0x03);
    Chirp_SendByte(addr_h);
    Chirp_SendByte(addr_l);
    Chirp_SendByte(netid);

    while (UART_GetFlagStatus(CHIRP_UARTx, UART_FLAG_TC) == RESET);

    start = GetTick();
    while ((GetTick() - start) < 200)
    {
        uint8_t tmp;
        if (Chirp_RxFifoRead(&tmp, 1) == 1)
        {
            if (rx_cnt < sizeof(rx_buf))
                rx_buf[rx_cnt++] = tmp;
            if (rx_cnt >= 6) break;
        }
    }

    printf("[CHIRP] RX (%d bytes):", rx_cnt);
    for (i = 0; i < rx_cnt; i++) printf(" %02X", rx_buf[i]);
    printf("\r\n");

    if (rx_cnt >= 6 &&
        rx_buf[0] == 0xC1 && rx_buf[1] == 0x00 && rx_buf[2] == 0x03)
    {
        ret = 1;
        printf("[CHIRP] Address set OK\r\n");
    }
    else
    {
        printf("[CHIRP] Address set failed\r\n");
    }

    Chirp_EnterDefaultMode();

    Delay_ms(5);
    return ret;
}

/******************************************************************************
 * 启用定点传输（读改写 REG3 bit6 = 1）
 ******************************************************************************/
uint8_t Chirp_EnableFixedPoint(void)
{
    uint8_t rx_buf[8] = {0};
    uint8_t rx_cnt = 0;
    uint32_t start;
    uint8_t ret = 0;
    uint8_t i;

    Chirp_WaitTxFifoEmpty();

    Chirp_EnterConfigMode();

    printf("[CHIRP] Enable fixed-point mode (REG3 bit6)\r\n");

    Chirp_RxFifoFlush();

    Chirp_SendByte(0xC1);
    Chirp_SendByte(0x06);
    Chirp_SendByte(0x01);

    while (UART_GetFlagStatus(CHIRP_UARTx, UART_FLAG_TC) == RESET);

    start = GetTick();
    while ((GetTick() - start) < 200)
    {
        uint8_t tmp;
        if (Chirp_RxFifoRead(&tmp, 1) == 1)
        {
            if (rx_cnt < sizeof(rx_buf))
                rx_buf[rx_cnt++] = tmp;
            if (rx_cnt >= 4) break;
        }
    }

    printf("[CHIRP] RX (%d bytes):", rx_cnt);
    for (i = 0; i < rx_cnt; i++) printf(" %02X", rx_buf[i]);
    printf("\r\n");

    if (rx_cnt < 4 || rx_buf[0] != 0xC1 || rx_buf[1] != 0x06 || rx_buf[2] != 0x01)
    {
        printf("[CHIRP] Read REG3 failed\r\n");
        Chirp_EnterDefaultMode();
        Delay_ms(5);
        return 0;
    }

    uint8_t reg3 = rx_buf[3];
    reg3 |= 0x40;

    Chirp_SendByte(0xC0);
    Chirp_SendByte(0x06);
    Chirp_SendByte(0x01);
    Chirp_SendByte(reg3);

    while (UART_GetFlagStatus(CHIRP_UARTx, UART_FLAG_TC) == RESET);

    rx_cnt = 0;
    start = GetTick();
    while ((GetTick() - start) < 200)
    {
        uint8_t tmp;
        if (Chirp_RxFifoRead(&tmp, 1) == 1)
        {
            if (rx_cnt < sizeof(rx_buf))
                rx_buf[rx_cnt++] = tmp;
            if (rx_cnt >= 4) break;
        }
    }

    printf("[CHIRP] RX (%d bytes):", rx_cnt);
    for (i = 0; i < rx_cnt; i++) printf(" %02X", rx_buf[i]);
    printf("\r\n");

    if (rx_cnt >= 4 && rx_buf[0] == 0xC1 && rx_buf[1] == 0x06 && rx_buf[2] == 0x01)
    {
        ret = 1;
        printf("[CHIRP] REG3 set OK (0x%02X)\r\n", rx_buf[3]);
    }
    else
    {
        printf("[CHIRP] Enable fixed-point failed\r\n");
    }

    Chirp_EnterDefaultMode();

    Delay_ms(5);
    return ret;
}

/******************************************************************************
 * 发射功率配置
 ******************************************************************************/
uint8_t Chirp_SetPower(uint8_t power)
{
    uint8_t rx_buf[8] = {0};
    uint8_t rx_cnt = 0;
    uint32_t start;
    uint8_t ret = 0;
    uint8_t i;

    if (power > 3)
    {
        printf("[CHIRP] Power invalid: %d (must be 0~3)\r\n", power);
        return 0;
    }

    Chirp_WaitTxFifoEmpty();

    Chirp_EnterConfigMode();

    printf("[CHIRP] Set Power = %d\r\n", power);

    Chirp_RxFifoFlush();

    Chirp_SendByte(0xC1);
    Chirp_SendByte(0x04);
    Chirp_SendByte(0x01);

    while (UART_GetFlagStatus(CHIRP_UARTx, UART_FLAG_TC) == RESET);

    start = GetTick();
    while ((GetTick() - start) < 200)
    {
        uint8_t tmp;
        if (Chirp_RxFifoRead(&tmp, 1) == 1)
        {
            if (rx_cnt < sizeof(rx_buf))
                rx_buf[rx_cnt++] = tmp;
            if (rx_cnt >= 4) break;
        }
    }

    if (rx_cnt < 4 || rx_buf[0] != 0xC1 || rx_buf[1] != 0x04 || rx_buf[2] != 0x01)
    {
        printf("[CHIRP] Read REG1 failed\r\n");
        Chirp_EnterDefaultMode();
        Delay_ms(5);
        return 0;
    }

    uint8_t reg1 = rx_buf[3];
    reg1 = (reg1 & 0xFC) | (power & 0x03);

    Chirp_SendByte(0xC0);
    Chirp_SendByte(0x04);
    Chirp_SendByte(0x01);
    Chirp_SendByte(reg1);

    while (UART_GetFlagStatus(CHIRP_UARTx, UART_FLAG_TC) == RESET);

    rx_cnt = 0;
    start = GetTick();
    while ((GetTick() - start) < 200)
    {
        uint8_t tmp;
        if (Chirp_RxFifoRead(&tmp, 1) == 1)
        {
            if (rx_cnt < sizeof(rx_buf))
                rx_buf[rx_cnt++] = tmp;
            if (rx_cnt >= 4) break;
        }
    }

    if (rx_cnt >= 4 && rx_buf[0] == 0xC1 && rx_buf[1] == 0x04 && rx_buf[2] == 0x01)
    {
        ret = 1;
        printf("[CHIRP] Power set OK (0x%02X)\r\n", rx_buf[3]);
    }
    else
    {
        printf("[CHIRP] Set Power failed\r\n");
    }

    Chirp_EnterDefaultMode();

    Delay_ms(5);
    return ret;
}

/******************************************************************************
 * WOR 唤醒后延时配置（写 0x09 低字节、0x0A 高字节）
 * 手册格式：C0 09 02 E8 03（小端），即低字节先写
 * delay_ms 范围 0~65535
 * 返回 1=成功 0=失败
 ******************************************************************************/
uint8_t Chirp_SetWorDelay(uint16_t delay_ms)
{
    uint8_t rx_buf[8] = {0};
    uint8_t rx_cnt = 0;
    uint32_t start;
    uint8_t ret = 0;
    uint8_t i;

    Chirp_WaitTxFifoEmpty();
    Chirp_EnterConfigMode();
    printf("[CHIRP] Set WOR delay = %u ms\r\n", delay_ms);
    Chirp_RxFifoFlush();

    /* 手册要求：C0 09 02 + 低字节 + 高字节 */
    Chirp_SendByte(0xC0);
    Chirp_SendByte(0x09);
    Chirp_SendByte(0x02);
    Chirp_SendByte((uint8_t)(delay_ms & 0xFF));        /* 低字节 */
    Chirp_SendByte((uint8_t)((delay_ms >> 8) & 0xFF)); /* 高字节 */
    while (UART_GetFlagStatus(CHIRP_UARTx, UART_FLAG_TC) == RESET);

    start = GetTick();
    while ((GetTick() - start) < 200)
    {
        uint8_t tmp;
        if (Chirp_RxFifoRead(&tmp, 1) == 1)
        {
            if (rx_cnt < sizeof(rx_buf)) rx_buf[rx_cnt++] = tmp;
            if (rx_cnt >= 3) break;   /* 回包 >=3 头即可 */
        }
    }

    printf("[CHIRP] RX (%d bytes):", rx_cnt);
    for (i = 0; i < rx_cnt; i++) printf(" %02X", rx_buf[i]);
    printf("\r\n");

    if (rx_cnt >= 3 && rx_buf[0] == 0xC1 && rx_buf[1] == 0x09 && rx_buf[2] == 0x02)
    {
        ret = 1;
        printf("[CHIRP] WOR delay set OK\r\n");
    }
    else
    {
        printf("[CHIRP] WOR delay set failed\r\n");
    }

    Chirp_EnterDefaultMode();
    Delay_ms(5);
    return ret;
}

/******************************************************************************
 * WOR 监听周期配置（写 REG3 0x06 的 bit2~bit0）
 * 周期 T = (WOR + 1) * 500ms
 * 0=500ms 1=1000ms 2=1500ms 3=2000ms 4=2500ms 5=3000ms 6=3500ms 7=4000ms
 * 返回 1=成功 0=失败
 ******************************************************************************/
uint8_t Chirp_SetWorPeriod(uint16_t period_ms)
{
    uint8_t wor;
    uint8_t rx_buf[8] = {0};
    uint8_t rx_cnt = 0;
    uint32_t start;
    uint8_t ret = 0;
    uint8_t i;

    /* 周期 → WOR 值 */
    if (period_ms <= 500)       wor = 0;
    else if (period_ms <= 1000) wor = 1;
    else if (period_ms <= 1500) wor = 2;
    else if (period_ms <= 2000) wor = 3;
    else if (period_ms <= 2500) wor = 4;
    else if (period_ms <= 3000) wor = 5;
    else if (period_ms <= 3500) wor = 6;
    else                        wor = 7;

    Chirp_WaitTxFifoEmpty();
    Chirp_EnterConfigMode();
    printf("[CHIRP] Set WOR period = %u ms (WOR=%u)\r\n", period_ms, wor);
    Chirp_RxFifoFlush();

    /* 读 REG3（地址 0x06） */
    Chirp_SendByte(0xC1);
    Chirp_SendByte(0x06);
    Chirp_SendByte(0x01);
    while (UART_GetFlagStatus(CHIRP_UARTx, UART_FLAG_TC) == RESET);

    start = GetTick();
    while ((GetTick() - start) < 200)
    {
        uint8_t tmp;
        if (Chirp_RxFifoRead(&tmp, 1) == 1)
        {
            if (rx_cnt < sizeof(rx_buf)) rx_buf[rx_cnt++] = tmp;
            if (rx_cnt >= 4) break;
        }
    }

    if (rx_cnt < 4 || rx_buf[0] != 0xC1 || rx_buf[1] != 0x06 || rx_buf[2] != 0x01)
    {
        printf("[CHIRP] Read REG3 failed\r\n");
        Chirp_EnterDefaultMode();
        Delay_ms(5);
        return 0;
    }

    /* 清低 3 位，写入新 WOR 值 */
    uint8_t reg3 = (rx_buf[3] & 0xF8) | (wor & 0x07);

    Chirp_SendByte(0xC0);
    Chirp_SendByte(0x06);
    Chirp_SendByte(0x01);
    Chirp_SendByte(reg3);
    while (UART_GetFlagStatus(CHIRP_UARTx, UART_FLAG_TC) == RESET);

    rx_cnt = 0;
    start = GetTick();
    while ((GetTick() - start) < 200)
    {
        uint8_t tmp;
        if (Chirp_RxFifoRead(&tmp, 1) == 1)
        {
            if (rx_cnt < sizeof(rx_buf)) rx_buf[rx_cnt++] = tmp;
            if (rx_cnt >= 4) break;
        }
    }

    if (rx_cnt >= 4 && rx_buf[0] == 0xC1 && rx_buf[1] == 0x06 && rx_buf[2] == 0x01)
    {
        ret = 1;
        printf("[CHIRP] WOR period set OK (REG3=0x%02X)\r\n", rx_buf[3]);
    }
    else
    {
        printf("[CHIRP] WOR period set failed\r\n");
    }

    Chirp_EnterDefaultMode();
    Delay_ms(5);
    return ret;
}


/******************************************************************************
 * 进入 WOR 模式（模式 1：M0=1, M1=0）
 * 与 EnterDefaultMode 区别：M0 高电平
 ******************************************************************************/
void Chirp_EnterWorMode(void)
{
    CHIRP_M0_HIGH();
    CHIRP_M1_LOW();

    uint32_t start = GetTick();
    while (CHIRP_AUX_READ() == GPIO_Pin_RESET && (GetTick() - start) < 500);

    Delay_ms(10);
}

/**
 * @brief 休眠前关 Chirp 接收中断 + NVIC（本版不 reset 时钟）
 */
void Chirp_SleepEnable(void)
{
    UART_ITConfig(CHIRP_UARTx, UART_IT_RC, DISABLE);
    NVIC_DisableIRQ(UART1_IRQn);
}

/**
 * @brief 唤醒后恢复 Chirp：
 *        1) PA05 回 GPIO_IT_NONE 轮询态
 *        2) 清 FIFO
 *        3) 恢复 UART1 RC 中断 + NVIC
 *        4) 恢复 M0/M1 到 WOR 模式（M0=1, M1=0）
 */
void Chirp_WakeupRestore(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    /* 1. PA05 回轮询态，避免业务期误进中断 */
    GPIO_InitStructure.Pins = CHIRP_AUX_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT_PULLUP;
    GPIO_InitStructure.IT   = GPIO_IT_NONE;
    GPIO_Init(CHIRP_AUX_PORT, &GPIO_InitStructure);
    CW_GPIOA->ICR_f.PIN5 = 0;

    /* 2. 保留 RX FIFO（唤醒首包交给 App_ChirpTask 处理），只清 TX */
    Chirp_TxFifoInit();

    /* 3. 恢复接收中断 */
    UART_ITConfig(CHIRP_UARTx, UART_IT_RC, ENABLE);
    NVIC_EnableIRQ(UART1_IRQn);

    /* 4. 恢复模式*/
    //Chirp_EnterWorMode();
		Chirp_EnterDefaultMode();
}

/**
 * @brief 休眠前配置 PA05（Chirp AUX）为下降沿中断唤醒
 *        AUX 外部 100K 上拉，平时高，有数据时下降沿
 */
void Chirp_GpioSleepEnable(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.Pins = CHIRP_AUX_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT_PULLUP;
    GPIO_InitStructure.IT   = GPIO_IT_FALLING;
    GPIO_Init(CHIRP_AUX_PORT, &GPIO_InitStructure);

    /* 按位清 PA05 挂起标志 */
    CW_GPIOA->ICR_f.PIN5 = 0;

    /* 使能 NVIC */
    NVIC_EnableIRQ(GPIOA_IRQn);
}

/******************************************************************************
 * ChirpIoT 上电初始化
 ******************************************************************************/
uint8_t Chirp_ConfigInit(void)
{
    uint8_t channel = 0;

    if (!Chirp_ReadChannel(&channel))
    {
        printf("ChirpIoT read failed\r\n");
        return 0;
    }
    printf("ChirpIoT Channel: %d\r\n", channel);

    SysConfig_t *cfg = AppConfig_Get();
    if (Chirp_SetAddress(cfg->local_addr, cfg->netid))
        printf("ChirpIoT Address set: 0x%04X, NetID: 0x%02X\r\n", cfg->local_addr, cfg->netid);
    else
        printf("ChirpIoT Address set failed\r\n");

    if (Chirp_EnableFixedPoint())
        printf("ChirpIoT fixed-point mode enabled\r\n");
    else
        printf("ChirpIoT enable fixed-point failed\r\n");

    /* ===== WOR 模式参数配置（开发期 1000ms） ===== */
    if (Chirp_SetWorPeriod(1000))
        printf("ChirpIoT WOR period set: 1000ms\r\n");
    else
        printf("ChirpIoT WOR period set failed\r\n");

    if (Chirp_SetWorDelay(1000))
        printf("ChirpIoT WOR delay set: 1000ms\r\n");
    else
        printf("ChirpIoT WOR delay set failed\r\n");

#if APP_DEV_MODE
    if (Chirp_SetPower(0))
        printf("ChirpIoT Power set: 22dBm (Dev mode)\r\n");
		
//    if (Chirp_SetPower(3))
//        printf("ChirpIoT Power set: 14dBm (Dev mode)\r\n");
#endif

    Chirp_EnterDefaultMode();
    printf("ChirpIoT in default mode\r\n");

    Chirp_EnterReceiveMode();
    printf("ChirpIoT in receive mode\r\n");

    return 1;
}
