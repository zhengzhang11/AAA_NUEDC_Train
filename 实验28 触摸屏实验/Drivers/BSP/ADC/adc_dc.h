/**
 * @file  adc_dc.h
 * @brief High-precision weak DC voltage acquisition for ATK-Explorer STM32F407 V3.
 *        ADC1 CH5 (PA5), triggered by TIM3 TRGO at 10 kHz.
 *        DMA2 Stream4 CH0, circular buffer, 1000 samples (100 ms = 5 * 50Hz periods).
 *        Perfect 50Hz cancellation by averaging an integer multiple of 50Hz periods.
 *
 * Hardware front-end:
 *   Vin(0-100mV) -> RC LP (10k+1uF, fc=16Hz) -> U1A(buffer) -> U1B(gain=21) -> PA5
 *   V_PA5 = Vin * 21 (max 2.1V for 100mV in, safe for 3.3V Vref)
 *
 * All comments pure ASCII (Keil GBK-safe).
 */
#ifndef __ADC_DC_H
#define __ADC_DC_H

#include "./SYSTEM/sys/sys.h"

/* ---- GPIO ---------------------------------------------------------------- */
#define ADC_DC_GPIO_PORT        GPIOA
#define ADC_DC_GPIO_PIN         GPIO_PIN_5
#define ADC_DC_GPIO_CLK_EN()    do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)

/* ---- ADC ----------------------------------------------------------------- */
#define ADC_DC_ADCx             ADC1
#define ADC_DC_CHANNEL          ADC_CHANNEL_5
#define ADC_DC_ADC_CLK_EN()     do{ __HAL_RCC_ADC1_CLK_ENABLE(); }while(0)

/* ---- DMA (ADC1 -> DMA2 Stream4 Channel0) -------------------------------- */
#define ADC_DC_DMA_STREAM       DMA2_Stream4
#define ADC_DC_DMA_CHANNEL      DMA_CHANNEL_0
#define ADC_DC_DMA_CLK_EN()     do{ __HAL_RCC_DMA2_CLK_ENABLE(); }while(0)
#define ADC_DC_DMA_IRQn         DMA2_Stream4_IRQn
#define ADC_DC_DMA_IRQHandler   DMA2_Stream4_IRQHandler

/* ---- Timer trigger (TIM3 TRGO at 10 kHz) -------------------------------- */
/* TIM3 clock: APB1*2 = 84 MHz; PSC=83, ARR=99 -> 84M/84/100 = 10 kHz       */
#define ADC_DC_TIM              TIM3
#define ADC_DC_TIM_CLK_EN()     do{ __HAL_RCC_TIM3_CLK_ENABLE(); }while(0)
#define ADC_DC_TIM_PSC          83u
#define ADC_DC_TIM_ARR          99u
#define ADC_DC_TIM_TRIG_SRC     ADC_EXTERNALTRIGCONV_T3_TRGO
#define ADC_DC_TIM_TRIG_EDGE    ADC_EXTERNALTRIGCONVEDGE_RISING

/* ---- Sampling parameters ------------------------------------------------ */
/* 1000 samples @ 10 kHz = 100 ms = exactly 5 periods of 50 Hz              */
/* Summing 5 complete 50Hz cycles -> 50Hz component averages to ZERO         */
#define ADC_DC_BUF_SIZE         1000u       /* DMA buffer (uint16_t words)   */
#define ADC_DC_FS               10000u      /* sample rate Hz                */

/* ---- Analogue front-end parameters -------------------------------------- */
#define ADC_DC_HW_GAIN          21.0f       /* op-amp gain (1 + Rf/Rg)       */
#define ADC_DC_VREF             3.3f        /* ADC reference voltage (V)     */
#define ADC_DC_FULLSCALE        0.100f      /* max input voltage (V)         */

/* ---- Exported globals --------------------------------------------------- */
extern uint16_t          g_adc_dc_buf[ADC_DC_BUF_SIZE];
extern volatile uint8_t  g_adc_dc_ready;   /* set to 1 by DMA TC callback   */

/* ---- Public API --------------------------------------------------------- */
void    adc_dc_init(void);    /* init GPIO + TIM3 + ADC + DMA; starts acq   */
void    adc_dc_stop(void);    /* pause acquisition (for seek/calibration)    */
void    adc_dc_start(void);   /* resume acquisition                          */

/* Compute mean of current buffer; result in ADC counts (0..4095) */
uint32_t adc_dc_mean_raw(void);

/* Compute mean voltage at PA5 pin (V) */
float   adc_dc_mean_pa5(void);

#endif /* __ADC_DC_H */
