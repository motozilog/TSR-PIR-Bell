/**
 * @file bsp_adc.c
 * @brief 电池 ADC 采样 PB04 (ADC_IN11)
 *        初始化：同步采 10 次，去极值平均
 *        之后：异步采，中断存结果
 */

#include "bsp_adc.h"
#include "bsp_systick.h"

#define BATT_INIT_SAMPLES   10   /* 初始化采样次数 */
#define BATT_INIT_INTERVAL  10   /* 采样间隔 ms */

/* 最近一次结果 */
static volatile uint16_t s_last_raw = 0;
static volatile uint8_t  s_done_flag = 0;

/******************************************************************************
 * 内部函数：同步采一次（初始化时用，不开中断）
 ******************************************************************************/
static uint16_t BATT_SampleSyncOnce(void)
{
    uint32_t timeout = ADC_WAIT_TIMEOUT;

    ADC_SoftwareStartConvCmd(ENABLE);

    while (ADC_GetITStatus(ADC_IT_EOC) != SET && timeout--)
    {
        ;
    }

    if (timeout == 0) return 0;

    uint16_t val = ADC_GetConversionValue(ADC_RESULT_0);
    ADC_ClearITPendingBit(ADC_IT_EOC);
    return val;
}

/******************************************************************************
 * 初始化
 ******************************************************************************/
void BATT_Init(void)
{
    ADC_InitTypeDef adc = {0};

    /* 1. 配 ADC */
    PB04_ANALOG_ENABLE();

    adc.ADC_ClkDiv      = ADC_Clk_Div8;
    adc.ADC_ConvertMode = ADC_ConvertMode_Once;
    adc.ADC_SQREns      = ADC_SqrEns0to0;

    adc.ADC_IN0.ADC_InputChannel = ADC_InputCH11;
    adc.ADC_IN0.ADC_SampTime     = ADC_SampTime262Clk;

    ADC_Init(&adc);
    ADC_Enable();

    /* 2. 同步采 10 次去极值平均，得到初值 */
    (void)BATT_Measure10TimesAvg();

    /* 3. 开 EOC 中断 + NVIC（优先级 3，最低） */
    ADC_ITConfig(ADC_IT_EOC, ENABLE);

    NVIC_SetPriority(ADC_IRQn, 3);
    NVIC_EnableIRQ(ADC_IRQn);
}


/******************************************************************************
 * 同步采 10 次，去最大 2 最小 2，取中间 6 个平均
 * 返回平均 raw，并更新 s_last_raw
 ******************************************************************************/
uint16_t BATT_Measure10TimesAvg(void)
{
    uint16_t samples[BATT_INIT_SAMPLES];
    uint32_t sum = 0;
    uint8_t  i, j;

    for (i = 0; i < BATT_INIT_SAMPLES; i++)
    {
        samples[i] = BATT_SampleSyncOnce();
        Delay_ms(BATT_INIT_INTERVAL);
    }

    /* 排序（冒泡） */
    for (i = 0; i < BATT_INIT_SAMPLES - 1; i++)
    {
        for (j = 0; j < BATT_INIT_SAMPLES - 1 - i; j++)
        {
            if (samples[j] > samples[j + 1])
            {
                uint16_t tmp = samples[j];
                samples[j] = samples[j + 1];
                samples[j + 1] = tmp;
            }
        }
    }

    /* 去掉最小 2、最大 2，取中间 6 个平均 */
    for (i = 2; i < BATT_INIT_SAMPLES - 2; i++)
    {
        sum += samples[i];
    }
    s_last_raw = (uint16_t)(sum / (BATT_INIT_SAMPLES - 4));

    return s_last_raw;
}

/******************************************************************************
 * 异步触发一次转换
 ******************************************************************************/
void BATT_StartConversion(void)
{
    ADC_SoftwareStartConvCmd(ENABLE);
}

/******************************************************************************
 * 取最近结果
 ******************************************************************************/
uint16_t BATT_GetLastRaw(void)
{
    return s_last_raw;
}

uint16_t BATT_GetLastVoltage_mV(void)
{
    /* Vin(mV) = raw × 3.000V × 168 / 100 / 4095 × 1000
     *         = raw × 504000 / 409500
     */
    uint32_t mv = ((uint32_t)s_last_raw * 504000UL) / 409500UL;
    return (uint16_t)mv;
}

/******************************************************************************
 * 完成标志
 ******************************************************************************/
uint8_t BATT_IsConversionDone(void)
{
    return s_done_flag;
}

void BATT_ClearConversionDone(void)
{
    s_done_flag = 0;
}

/******************************************************************************
 * EOC 中断回调（由 interrupts_cw32l010.c 调用）
 ******************************************************************************/
void BATT_OnEocIsr(uint16_t raw)
{
    s_last_raw = raw;
    s_done_flag = 1;
}