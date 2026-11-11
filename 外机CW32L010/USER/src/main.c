/**
 * @file main.c
 * @author WHXY
 * @brief CW32L010 ChirpIoT 门铃 - 主框架
 */

#include "main.h"
#include <stdlib.h>
#include "bsp_systick.h"
#include "bsp_uart.h"
#include "bsp_key.h"
#include "bsp_pir.h"
#include "bsp_adc.h"
#include "bsp_full.h"
#include "bsp_led.h"
#include "bsp_rtc.h"
#include "bsp_chirp.h"
#include "app_chirp.h"
#include "app_config.h"
#include "app_config_mgr.h"
#include "cw32l010_sysctrl.h"
#include "cw32l010_iwdt.h"
#include "utils.h"

/******************************************************************************
 * 休眠/唤醒参数
 ******************************************************************************/
#define AWAKE_WINDOW_MS         3000   /* 唤醒后业务窗口 */
#define WDT_FEED_INTERVAL_SEC   30     /* 单次最大睡眠时间，用于喂狗 */

/******************************************************************************
 * RCC
 ******************************************************************************/
static void RCC_Configuration(void)
{
    SYSCTRL_HSI_Enable(SYSCTRL_HSIOSC_DIV16);   /* HSI 48MHz / 16 = 3MHz */

    /* 必须提前开启 LSI，给看门狗和 RTC 使用 */
    SYSCTRL_LSI_Enable(); 

    SYSCTRL_AHBPeriphClk_Enable(SYSCTRL_AHB_PERIPH_GPIOA, ENABLE);
    SYSCTRL_AHBPeriphClk_Enable(SYSCTRL_AHB_PERIPH_GPIOB, ENABLE);
    SYSCTRL_AHBPeriphClk_Enable(SYSCTRL_AHB_PERIPH_FLASH, ENABLE);
    
    /* 使能 IWDT 时钟 */
    SYSCTRL_APBPeriphClk_Enable2(SYSCTRL_APB2_PERIPH_IWDT, ENABLE);

    DEBUG_UART_APBClkENx(DEBUG_UART_CLK, ENABLE);
    CHIRP_UART_APBClkENx(CHIRP_UART_CLK, ENABLE);
}

/******************************************************************************
 * 休眠状态机
 ******************************************************************************/
typedef enum {
    SLEEP_ARMED = 0,
    AWAKE_WINDOW
} SleepState_t;

// 计算距离下次业务唤醒的秒数 (不限制最大时长)
static uint32_t GetNextBusinessWakeupInterval(void)
{
    uint32_t now_ts = RTC_GetUnixTime();
    uint32_t compile_ts = Utils_GetCompileTimeUnix();

    if (now_ts < compile_ts) {
        return 60;
    }

#if APP_DEV_MODE
    uint8_t sec = now_ts % 60;
    if (sec < 1) return 1;
    if (sec < 31) return (31 - sec);
    return (60 - sec + 1); 
#else
    uint32_t sec_of_day = now_ts % 86400;
    uint32_t min_of_day = sec_of_day / 60;
    uint32_t sec_of_min = sec_of_day % 60;
    uint32_t next_min = (min_of_day / 15 + 1) * 15;
    if (min_of_day % 15 == 0 && sec_of_min < 1) {
        next_min = min_of_day;
    }
    uint32_t next_sec_of_day = next_min * 60 + 1;
    if (next_sec_of_day <= sec_of_day) {
        next_sec_of_day += 86400; 
    }
    return (next_sec_of_day - sec_of_day);
#endif
}

/******************************************************************************
 * 主函数
 ******************************************************************************/
volatile uint8_t gKeyStatus = 0;
volatile uint8_t gWakeupSource = 0;

int32_t main(void)
{
    uint32_t     last_led    = 0;
    SleepState_t sleep_state = SLEEP_ARMED;
    uint32_t     awake_start = 0;

    RCC_Configuration();
    SysTick_Init();

    /* 等待 LSI 稳定 */
    Delay_ms(5);

    /* ===== 1. 先初始化串口 ===== */
    Debug_UART_Init(); 
    printf("\r\n\r\nReset flag: 0x%08X\r\n", SYSCTRL_GetAllRstFlag());

    /* ===== 2. 手工配置看门狗 (绕过官方库函数) ===== */
    // 按照手册：解锁 -> 配 CR/ARR -> 启动 -> 喂狗
    CW_IWDT->KR = IWDT_UNLOCK_KEY;         // 解锁
    CW_IWDT->CR = IWDT_Prescaler_DIV512 |  // 预分频：LSI(32K)/512 = 62.5Hz
                  IWDT_SLEEP_CONTINUE |    // DeepSleep 继续运行
                  IWDT_OVERFLOW_ACTION_RESET; // 溢出复位
    CW_IWDT->ARR = 4095;                   // 重载值 4095 (4095/62.5 ≈ 65 秒)
    CW_IWDT->KR = IWDT_RUN_KEY;            // 启动看门狗
    CW_IWDT->KR = IWDT_REFRESH_KEY;        // 喂一次狗，加载 ARR 到计数器
    CW_IWDT->KR = IWDT_LOCK_KEY;           // 锁定寄存器

    /* ===== 3. 应用层初始化 ===== */
    AppChirp_Init();

    AppConfig_Init();
    if (AppConfig_LoadFromFlash()) {
        printf("[CFG] Loaded from Flash\r\n");
    } else {
        printf("[CFG] No valid config in Flash, using defaults\r\n");
    }

    SysConfig_t *cfg = AppConfig_Get();
    printf("addr=0x%04X, netid=0x%02X, ch=%u, peer=0x%04X, sum=0x%02X\r\n",
           cfg->local_addr, cfg->netid, cfg->channel, cfg->peer_addr, cfg->checksum);

    LED_Init();
    Key_Init();
    CfgKey_Init();
    PIR_Init();
    BATT_Init();
    BSP_RTC_Init();
    FULL_Init();
    Chirp_Init();

    Delay_ms(200);
    CW_IWDT->KR = IWDT_REFRESH_KEY; // 耗时延时后喂狗

    printf("TSR PIR Doorbell CW32L010\r\n");
    printf("By motozilog V1.0, compile time: %s %s\r\n", __DATE__, __TIME__);
    printf("ChirpIoT UART1 (PB00/PB01) @ 9600, 8N1\r\n");

    /* 配置 Chirp，这里耗时较长 */
    Chirp_ConfigInit(); 
    CW_IWDT->KR = IWDT_REFRESH_KEY; // Chirp 配置完后立刻喂狗
    printf("\r\n");

    App_ChirpInit();

    /* 启动上报时间和CW32的RTC值 */
    AppChirp_OnBoot();
    AppChirp_RequestRtc();
    
    CW_IWDT->KR = IWDT_REFRESH_KEY; // 进主循环前喂狗

    while (1)
    {
        /* 任务循环处喂狗 */
        CW_IWDT->KR = IWDT_REFRESH_KEY;

			
			  /* ===== 临时测试：卡死 100 秒，测试看门狗 ===== */
        //for(volatile uint32_t _i = 0; _i < 400000000; _i++); 
			
        /* ===== 休眠状态机 ===== */
        if (sleep_state == SLEEP_ARMED)
        {
            Key_GpioSleepEnable();
            Chirp_GpioSleepEnable();
            PIR_GpioSleepEnable();
            FULL_GpioSleepEnable();

            Chirp_WaitTxIdle();
            Chirp_EnterWorMode();

            uint32_t biz_sec = GetNextBusinessWakeupInterval();
            uint16_t sleep_sec = (biz_sec > WDT_FEED_INTERVAL_SEC) ? WDT_FEED_INTERVAL_SEC : biz_sec;
            
            RTC_SetAwakeTimer(sleep_sec);

            uint8_t planned_src = (sleep_sec < biz_sec) ? WAKEUP_SRC_WDT_FEED : WAKEUP_SRC_RTC_TIMER;
            gWakeupSource = planned_src;

            if (planned_src != WAKEUP_SRC_WDT_FEED)
            {
                RTC_DateTime_t dt;
                RTC_GetDateTime(&dt);
                printf("Sleep %u s (Business), at %04u-%02u-%02u %02u:%02u:%02u\r\n",
                       sleep_sec, dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
            }
            
						GPIO_WritePin(LED_PORT, LED_PIN, GPIO_Pin_RESET); // 休眠前确保 LED 熄灭

            Debug_UART_Close();
            Chirp_SleepEnable();
            RTC_SleepEnable();

            SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
            SYSCTRL_GotoDeepSleep();

            /* ===== 唤醒后 ===== */
            SYSCTRL_HSI_Enable(SYSCTRL_HSIOSC_DIV16);
            SysTick_Init();

            /* 如果是静默喂狗，立刻喂狗然后接着睡 */
            if (planned_src == WAKEUP_SRC_WDT_FEED && gWakeupSource == WAKEUP_SRC_WDT_FEED)
            {
                CW_IWDT->KR = IWDT_REFRESH_KEY;
                continue; 
            }
            
            /* 业务唤醒 */
            Debug_UART_Init(); 
            uint8_t wk = gWakeupSource;
            gWakeupSource = 0;

            Key_Init();
            PIR_Init();
            FULL_Init();
            RTC_WakeupRestore();
            Chirp_WakeupRestore();
            
            CW_IWDT->KR = IWDT_REFRESH_KEY;

            RTC_DateTime_t dt;
            RTC_GetDateTime(&dt);
            if (wk == 1)      printf("Wakeup by PA01 (KEY) at %04u-%02u-%02u %02u:%02u:%02u\r\n", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
            else if (wk == 2) printf("Wakeup by PA05 (Chirp AUX) at %04u-%02u-%02u %02u:%02u:%02u\r\n", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
            else if (wk == 3) printf("Wakeup by RTC Timer at %04u-%02u-%02u %02u:%02u:%02u\r\n", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
            else if (wk == 4) printf("Wakeup by PA02 (PIR) at %04u-%02u-%02u %02u:%02u:%02u\r\n", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
            else if (wk == 5) printf("Wakeup by PB02 (FULL) at %04u-%02u-%02u %02u:%02u:%02u\r\n", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
            else              printf("Wakeup, source unknown at %04u-%02u-%02u %02u:%02u:%02u\r\n", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);

            if (wk == 3) {
                if (RTC_GetUnixTime() >= Utils_GetCompileTimeUnix()) {
                    AppChirp_OnBatterySampleDone();
                }
            }
            if (wk == 4) {
                Delay_ms(20); // 刚唤醒时等电源稳定								
                AppChirp_OnPirTriggered();
            }
            if (wk == 5) {
                if (FULL_Scan()) {
										Delay_ms(20); // 刚唤醒时等电源稳定								
                    AppChirp_OnChargeFull();
                }
            }

            awake_start = GetTick();
            sleep_state = AWAKE_WINDOW;
        }
        else if (sleep_state == AWAKE_WINDOW)
        {
            CW_IWDT->KR = IWDT_REFRESH_KEY;

            if ((GetTick() - awake_start) >= AWAKE_WINDOW_MS)
            {
                CW_IWDT->KR = IWDT_REFRESH_KEY;
                sleep_state = SLEEP_ARMED;
            }
        }

        Debug_UART_Poll();

				//这里是每0.5秒闪一次，用于开局测试
//        if ((GetTick() - last_led) >= 500) {
//            last_led = GetTick();
//            GPIO_TogglePin(LED_PORT, LED_PIN);
//        }

        if (BATT_IsConversionDone()) {
            BATT_ClearConversionDone();
            AppChirp_OnBatterySampleDone();
        }

        switch (Key_Scan()) {
            case KEY_EVENT_PRESS:
                AppChirp_OnKeyPressed();
                printf("[KEY] Pressed\r\n");
                break;
            case KEY_EVENT_RELEASE:
                printf("[KEY] Released\r\n");
                break;
            default: break;
        }

        if (PIR_Scan()) {
            AppChirp_OnPirTriggered();
            printf("[PIR] Motion detected\r\n");
        }

        if (FULL_Scan()) {
            AppChirp_OnChargeFull();
        }

        App_ChirpTask();
        
        CW_IWDT->KR = IWDT_REFRESH_KEY; // 业务末尾喂狗
    }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif