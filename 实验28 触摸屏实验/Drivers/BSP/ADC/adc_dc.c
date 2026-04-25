/**
 * @file  adc_dc.c
 * @brief High-precision weak DC voltage acquisition.
 *        ADC1 CH5 (PA5) triggered by TIM3 TRGO at 10 kHz.
 *        DMA2 Stream4 circular mode; HAL_ADC_ConvCpltCallback sets ready flag.
 *        1000-sample buffer covers 5 complete 50Hz periods for perfect rejection.
 *
 * Adapted from ALIENTEK experiment 19-2 (single-channel ADC DMA) with additions:
 *   - Timer-triggered (not software-start) for precise 10 kHz rate
 *   - Circular DMA for gapless acquisition
 *   - HAL callback mechanism
 *
 * All comments pure ASCII (Keil GBK-safe).
 */

#include "./BSP/ADC/adc_dc.h"
#include "./SYSTEM/delay/delay.h"

/* =========================================================================
 * Globals
 * ========================================================================= */
uint16_t         g_adc_dc_buf[ADC_DC_BUF_SIZE];
volatile uint8_t g_adc_dc_ready = 0u;

static ADC_HandleTypeDef  s_adc_handle;
static DMA_HandleTypeDef  s_dma_handle;
static TIM_HandleTypeDef  s_tim_handle;

/* =========================================================================
 * TIM3 TRGO initialisation (10 kHz, update event as TRGO)
 * ========================================================================= */
static void adc_dc_tim_init(void)
{
    ADC_DC_TIM_CLK_EN();

    s_tim_handle.Instance               = ADC_DC_TIM;
    s_tim_handle.Init.Prescaler         = ADC_DC_TIM_PSC;
    s_tim_handle.Init.CounterMode       = TIM_COUNTERMODE_UP;
    s_tim_handle.Init.Period            = ADC_DC_TIM_ARR;
    s_tim_handle.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_tim_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&s_tim_handle);

    /* Configure TRGO: update event -> ADC trigger */
    TIM_MasterConfigTypeDef mc;
    mc.MasterOutputTrigger = TIM_TRGO_UPDATE;
    mc.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&s_tim_handle, &mc);

    HAL_TIM_Base_Start(&s_tim_handle);   /* start free-running, no interrupt */
}

/* =========================================================================
 * DMA initialisation (DMA2 Stream4 CH0, memory circular)
 * ========================================================================= */
static void adc_dc_dma_init(void)
{
    ADC_DC_DMA_CLK_EN();

    s_dma_handle.Instance                 = ADC_DC_DMA_STREAM;
    s_dma_handle.Init.Channel             = ADC_DC_DMA_CHANNEL;
    s_dma_handle.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    s_dma_handle.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_dma_handle.Init.MemInc              = DMA_MINC_ENABLE;
    s_dma_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    s_dma_handle.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    s_dma_handle.Init.Mode                = DMA_CIRCULAR;        /* auto-restart */
    s_dma_handle.Init.Priority            = DMA_PRIORITY_HIGH;
    s_dma_handle.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&s_dma_handle);

    /* Link DMA handle to ADC */
    __HAL_LINKDMA(&s_adc_handle, DMA_Handle, s_dma_handle);

    /* Enable DMA TC interrupt */
    HAL_NVIC_SetPriority(ADC_DC_DMA_IRQn, 2u, 0u);
    HAL_NVIC_EnableIRQ(ADC_DC_DMA_IRQn);
}

/* =========================================================================
 * ADC initialisation (ADC1 CH5, timer-triggered, DMA)
 * ========================================================================= */
static void adc_dc_adc_init(void)
{
    /* --- GPIO --- */
    ADC_DC_GPIO_CLK_EN();
    GPIO_InitTypeDef gp;
    gp.Pin  = ADC_DC_GPIO_PIN;
    gp.Mode = GPIO_MODE_ANALOG;
    gp.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ADC_DC_GPIO_PORT, &gp);

    /* --- ADC --- */
    ADC_DC_ADC_CLK_EN();
    s_adc_handle.Instance                   = ADC_DC_ADCx;
    s_adc_handle.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4; /* 21 MHz */
    s_adc_handle.Init.Resolution            = ADC_RESOLUTION_12B;
    s_adc_handle.Init.ScanConvMode          = DISABLE;
    s_adc_handle.Init.ContinuousConvMode    = DISABLE;   /* timer-triggered, not continuous */
    s_adc_handle.Init.DiscontinuousConvMode = DISABLE;
    s_adc_handle.Init.ExternalTrigConvEdge  = ADC_DC_TIM_TRIG_EDGE;
    s_adc_handle.Init.ExternalTrigConv      = ADC_DC_TIM_TRIG_SRC;
    s_adc_handle.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    s_adc_handle.Init.NbrOfConversion       = 1u;
    s_adc_handle.Init.DMAContinuousRequests = ENABLE;    /* DMA request on each conv */
    s_adc_handle.Init.EOCSelection          = ADC_EOC_SEQ_CONV;
    HAL_ADC_Init(&s_adc_handle);

    /* --- Channel --- */
    ADC_ChannelConfTypeDef ch;
    ch.Channel      = ADC_DC_CHANNEL;
    ch.Rank         = 1u;
    ch.SamplingTime = ADC_SAMPLETIME_480CYCLES; /* max: 480+12=492 cyc -> low noise */
    HAL_ADC_ConfigChannel(&s_adc_handle, &ch);
}

/* =========================================================================
 * Public: adc_dc_init
 * ========================================================================= */
void adc_dc_init(void)
{
    adc_dc_adc_init();
    adc_dc_dma_init();
    adc_dc_tim_init();

    /* Start ADC-DMA in circular mode; TC interrupt -> HAL_ADC_ConvCpltCallback */
    HAL_ADC_Start_DMA(&s_adc_handle,
                      (uint32_t *)g_adc_dc_buf,
                      ADC_DC_BUF_SIZE);
}

/* =========================================================================
 * Public: stop / start
 * ========================================================================= */
void adc_dc_stop(void)
{
    HAL_ADC_Stop_DMA(&s_adc_handle);
    HAL_TIM_Base_Stop(&s_tim_handle);
}

void adc_dc_start(void)
{
    HAL_TIM_Base_Start(&s_tim_handle);
    HAL_ADC_Start_DMA(&s_adc_handle,
                      (uint32_t *)g_adc_dc_buf,
                      ADC_DC_BUF_SIZE);
}

/* =========================================================================
 * Public: compute mean of buffer (raw ADC counts, 0..4095)
 * ========================================================================= */
uint32_t adc_dc_mean_raw(void)
{
    uint32_t sum = 0u;
    for (uint32_t i = 0u; i < ADC_DC_BUF_SIZE; i++)
    {
        sum += g_adc_dc_buf[i];
    }
    return sum / ADC_DC_BUF_SIZE;
}

/* =========================================================================
 * Public: mean voltage at PA5 (V)
 * ========================================================================= */
float adc_dc_mean_pa5(void)
{
    return (float)adc_dc_mean_raw() * ADC_DC_VREF / 4096.0f;
}

/* =========================================================================
 * DMA IRQ handler -> calls HAL, which calls HAL_ADC_ConvCpltCallback
 * ========================================================================= */
void ADC_DC_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&s_dma_handle);
}

/* =========================================================================
 * HAL ADC transfer-complete callback (called every ADC_DC_BUF_SIZE samples)
 * Sets the ready flag; application reads g_adc_dc_buf[] and clears the flag.
 * In circular mode the DMA auto-restarts; no action needed here.
 * ========================================================================= */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC_DC_ADCx)
    {
        g_adc_dc_ready = 1u;
    }
}
