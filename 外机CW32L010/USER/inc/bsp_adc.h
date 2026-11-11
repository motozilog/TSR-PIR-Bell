/**
 * @file bsp_adc.h
 * @brief 电池电量 ADC PB04 (ADC_IN11)
 */
#ifndef __BSP_ADC_H__
#define __BSP_ADC_H__

#include "main.h"

#define  BATT_ADC_PORT                 CW_GPIOB
#define  BATT_ADC_PIN                  GPIO_PIN_4

/* 初始化：同步采 10 次求初值 + 开中断 */
void     BATT_Init(void);

/* 触发一次转换（异步，不阻塞） */
void     BATT_StartConversion(void);

/* 取最近一次结果 */
uint16_t BATT_GetLastRaw(void);
uint16_t BATT_GetLastVoltage_mV(void);

//10次平均
uint16_t BATT_Measure10TimesAvg(void);

/* 完成标志（主循环用） */
uint8_t  BATT_IsConversionDone(void);
void     BATT_ClearConversionDone(void);

/* ADC EOC 中断回调（由 interrupts_cw32l010.c 调用） */
void     BATT_OnEocIsr(uint16_t raw);

#endif /* __BSP_ADC_H__ */