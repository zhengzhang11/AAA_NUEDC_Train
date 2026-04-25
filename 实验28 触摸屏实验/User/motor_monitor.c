/**
 * @file  motor_monitor.c
 * @brief Motor speed monitor + PI tuner for ATK-Explorer STM32F407 V3.
 *
 *  UART:   USART3,  PB11=RX  PB10=TX  115200 8-N-1
 *          ** Remove the P2 jumper on the board **
 *          Direct 3.3 V LVTTL connection to the motor-controller MCU.
 *
 *  RX frame (motor MCU -> this board):
 *      $<target>,<actual>\n
 *      e.g.  $10.50,-3.27\n
 *
 *  TX frame (this board -> motor MCU):
 *      #<kp>,<ki>,<amp>\n
 *      e.g.  #0.0054,0.0580,3.50\n
 *      Fields:
 *        kp  - proportional gain  (4 decimal places)
 *        ki  - integral gain      (4 decimal places)
 *        K   - multiplier for sim_target(): target = K*(0.06*sin(0.3Hz)+0.012*sin(2Hz))
              range 1.00-10.00, 2 decimal places
 *
 *  Display (portrait 240x320):
 *    y=  0.. 24  Title + [BACK]
 *    y= 25..184  10x10 grid (200 x 160 px, CELL_W=20 CELL_H=16)
 *                Left strip (x=0..39, 40px): Y-axis labels (rad/s)
 *    y=186..203  KP  slider
 *    y=206..223  KI  slider
 *    y=226..243  AMP slider
 *    y=247..262  Live values (overwritten in place, no flicker)
 *    y=265..280  Current KP / KI / AMP text
 *    y=283..298  Connection status
 *    y=301..316  UART port info (static, drawn once)
 *
 *  Keys (hold for continuous smooth zoom):
 *    KEY0 PE4 low : stretch X (zoom in,  more px/sample)
 *    KEY2 PE2 low : compress X (zoom out, fewer px/sample)
 *    KEY_UP PA0 hi: stretch Y (zoom in,  more px/(r/s))
 *    KEY1 PE3 low : compress Y (zoom out, fewer px/(r/s))
 *
 *  Touch [BACK] top-right to return to main menu.
 *  Drag on slider bars to adjust KP, KI, AMP.
 *
 *  All comments ASCII-only (Keil GBK-safe).
 */

#include "motor_monitor.h"
#include "chinese_font.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/TOUCH/touch.h"
#include "./SYSTEM/delay/delay.h"
#include "stm32f4xx_hal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* =========================================================================
 * GBK Chinese string constants (encoding-safe byte arrays)
 * ======================================================================= */
/* "返回"  fan-hui */
static const uint8_t MON_STR_BACK[]    = {0xB7,0xB5,0xBB,0xD8,0x00};
/* "电机监控"  dian-ji-jian-kong */
static const uint8_t MON_STR_TITLE[]   = {0xB5,0xE7,0xBB,0xFA,0xBC,0xE0,0xBF,0xD8,0x00};
/* "目标"  mu-biao */
static const uint8_t MON_STR_TGT[]     = {0xC4,0xBF,0xB1,0xEA,0x00};
/* "实际"  shi-ji */
static const uint8_t MON_STR_ACT[]     = {0xCA,0xB5,0xBC,0xCA,0x00};
/* "比例:"  bi-li */
static const uint8_t MON_STR_KP[]      = {0xB1,0xC8,0xC0,0xFD,0x3A,0x00};
/* "积分:"  ji-fen */
static const uint8_t MON_STR_KI[]      = {0xBB,0xFD,0xB7,0xD6,0x3A,0x00};
/* "增益:"  zeng-yi */
static const uint8_t MON_STR_AMP[]     = {0xD4,0xF6,0xD2,0xE6,0x3A,0x00};
/* "目标:"  mu-biao-colon */
static const uint8_t MON_STR_TGT_C[]   = {0xC4,0xBF,0xB1,0xEA,0x3A,0x00};
/* "实际:"  shi-ji-colon */
static const uint8_t MON_STR_ACT_C[]   = {0xCA,0xB5,0xBC,0xCA,0x3A,0x00};
/* "等待串口数据"  wait for UART data */
static const uint8_t MON_STR_WAIT[]    = {0xB5,0xC8,0xB4,0xFD,0xB4,0xAE,0xBF,0xDA,0xCA,0xFD,0xBE,0xDD,0x00};
/* "已连接 接收正常"  connected receiving normal */
static const uint8_t MON_STR_OK[]      = {0xD2,0xD1,0xC1,0xAC,0xBD,0xD3,0x20,0xBD,0xD3,0xCA,0xD5,0xD5,0xFD,0xB3,0xA3,0x00};
/* "串口超时 检查接线"  UART timeout check wiring */
static const uint8_t MON_STR_TOUT[]    = {0xB4,0xAE,0xBF,0xDA,0xB3,0xAC,0xCA,0xB1,0x20,0xBC,0xEC,0xB2,0xE9,0xBD,0xD3,0xCF,0xDF,0x00};
/* "状态:"  zhuang-tai-colon */
static const uint8_t MON_STR_STA_L[]   = {0xD7,0xB4,0xCC,0xAC,0x3A,0x00};

/* =========================================================================
 * Plot geometry  (portrait 240x320)
 * ======================================================================= */
#define OSC_LX      40          /* plot left  x  (label strip: x=0..39) */
#define OSC_TY      25          /* plot top   y                         */
#define OSC_W      200          /* plot width  (right edge x=239)       */
#define OSC_H      160          /* plot height (bottom  y=184)          */
#define GRID_COLS   10
#define GRID_ROWS   10
#define CELL_W     (OSC_W / GRID_COLS)   /* 20 px */
#define CELL_H     (OSC_H / GRID_ROWS)   /* 16 px */

/* =========================================================================
 * Slider geometry  (rows below the plot)
 * ======================================================================= */
#define SLI_LX       0   /* label left x   (0..39 = 40px = 5 chars@8px) */
#define SLI_BX      40   /* bar   left x   (aligned with plot)          */
#define SLI_BW     144   /* bar   width px                               */
#define SLI_VX     184   /* value left x   (184..239 = 56px = 7 chars)  */
#define SLI_H       18   /* row height px                                */
#define SLI_KP_Y   186
#define SLI_KI_Y   206
#define SLI_AM_Y   226

/* =========================================================================
 * Info rows  (all use 16-px font -> 8 px/char)
 * ======================================================================= */
#define INFO_VAL_Y  247   /* live TGT / ACT values      */
#define INFO_PAR_Y  265   /* KP KI AMP current settings */
#define INFO_STA_Y  283   /* connection status           */
#define INFO_UART_Y 301   /* static UART hint            */

/* =========================================================================
 * [BACK] button
 * ======================================================================= */
#define BACK_X     188
#define BACK_Y       4
#define BACK_W      50
#define BACK_H      18

/* =========================================================================
 * Colors
 * ======================================================================= */
#define C_BG        0x0000u   /* black - screen / plot background */
#define C_GRID      0x2104u   /* dark gray - grid lines           */
#define C_ZERO      0x4208u   /* mid  gray - zero-speed line      */
#define C_TGT       0xFFE0u   /* yellow  - target trace           */
#define C_ACT       0x07FFu   /* cyan    - actual trace           */
#define C_AXIS      0xFFFFu   /* white   - labels / borders       */
#define C_INFO      0x7BEFu   /* lt gray - info text              */
#define C_OK        0x07E0u   /* green   - connected status       */
#define C_WARN      0xFD20u   /* orange  - waiting / timeout      */
#define C_BACK_BG   0xF800u   /* red     - BACK button            */
#define C_TITLE     0x07E0u   /* green   - title text             */
#define C_SLI_TRK   0x4208u   /* slider track (dark gray)         */
#define C_SLI_KP    0x001Fu   /* blue  - KP slider fill           */
#define C_SLI_KI    0x07E0u   /* green - KI slider fill           */
#define C_SLI_AM    0xFD20u   /* orange- AMP slider fill          */

/* =========================================================================
 * PI / AMP defaults and ranges
 * ======================================================================= */
#define KP_DEF   0.0054f
#define KI_DEF   0.0580f
#define KP_MIN   (KP_DEF * 0.5f)   /* 0.0027 */
#define KP_MAX   (KP_DEF * 1.5f)   /* 0.0081 */
#define KI_MIN   (KI_DEF * 0.5f)   /* 0.0290 */
#define KI_MAX   (KI_DEF * 1.5f)   /* 0.0870 */
#define AMP_MIN  1.0f
#define AMP_MAX  10.0f
#define AMP_DEF  1.0f

/* =========================================================================
 * UART / buffer
 * ======================================================================= */
#define MON_BAUD         115200u
#define MON_BUF_SIZE     2048u
#define UART_LINE_MAX      64u
#define CONN_TIMEOUT_MS  3000u
#define TX_MIN_MS          50u   /* TX throttle: max 20 Hz */

/* =========================================================================
 * Zoom
 * ======================================================================= */
/* X-axis: samples visible in 200px plot width
 *   XSCALE_DEF=2.0 -> 100 samples visible at a time              */
#define XSCALE_DEF   2.0f
#define XSCALE_MIN   0.1f
#define XSCALE_MAX  20.0f

/* Y-axis: signal baseline = 0.06*sin(0.3Hz) + 0.012*sin(2Hz)
 *   peak amplitude ~ 0.072 rad/s
 *   YSCALE_DEF=600 -> 80px for 0.072r/s peak => fills ~half plot height nicely
 *   YSCALE_MIN= 40 -> zoom out: +-2 r/s range  (covers 10x AMP)
 *   YSCALE_MAX=6000-> zoom in:  +-0.013 r/s range (fine detail) */
#define YSCALE_DEF  600.0f
#define YSCALE_MIN   40.0f
#define YSCALE_MAX 6000.0f

#define ZOOM_K       1.05f
#define FRAME_MS      40u

/* =========================================================================
 * State
 * ======================================================================= */
typedef struct {
    /* zoom */
    float x_scale;
    float y_scale;
    float y_center;
    /* PI tuning */
    float kp, ki, amp;
    /* last received values */
    float last_tgt, last_act;
    /* timing */
    uint32_t last_rx_tick;
    uint32_t last_tx_tick;
    uint32_t last_info_tick;
    /* dirty flags (no redundant redraws = no flicker) */
    uint8_t wave_dirty;   /* plot region needs redraw     */
    uint8_t ctrl_dirty;   /* sliders / params need redraw */
    uint8_t info_dirty;   /* live values text needs update */
    uint8_t tx_pending;   /* TX frame to send             */
} MonState;

static MonState MS;

/* =========================================================================
 * Circular data buffer
 * ======================================================================= */
static float    buf_tgt[MON_BUF_SIZE];
static float    buf_act[MON_BUF_SIZE];
static volatile int32_t  buf_head  = -1;
static volatile uint32_t buf_count =  0;

/* =========================================================================
 * UART ISR state
 * ======================================================================= */
static UART_HandleTypeDef h_uart3;
static volatile char    rx_acc[UART_LINE_MAX];
static volatile uint8_t rx_idx      = 0;
static volatile uint8_t frame_ready = 0;
static          char    frame_buf[UART_LINE_MAX];

/* =========================================================================
 * USART3 IRQ - raw register access, no HAL callback
 * (avoids conflict with usart.c's HAL_UART_RxCpltCallback for USART1)
 * ======================================================================= */
void USART3_IRQHandler(void)
{
    uint32_t sr = USART3->SR;
    if (sr & USART_SR_RXNE) {
        char c = (char)(USART3->DR & 0xFFu);
        if (c == '$') {
            rx_idx = 0;
        } else if (c == '\n' || c == '\r') {
            if (rx_idx > 0u && !frame_ready) {
                rx_acc[rx_idx] = '\0';
                memcpy(frame_buf, (const void *)rx_acc, (size_t)rx_idx + 1u);
                frame_ready = 1u;
            }
            rx_idx = 0u;
        } else {
            if (rx_idx < (UART_LINE_MAX - 1u))
                rx_acc[rx_idx++] = c;
        }
    }
    if (sr & USART_SR_ORE) {   /* clear overrun to prevent ISR lockup */
        (void)USART3->SR;
        (void)USART3->DR;
    }
}

/* =========================================================================
 * UART init  (TX=PB10, RX=PB11, 115200 8N1)
 * ======================================================================= */
static void mon_uart_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_10 | GPIO_PIN_11;   /* TX and RX */
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOB, &gpio);

    h_uart3.Instance          = USART3;
    h_uart3.Init.BaudRate     = MON_BAUD;
    h_uart3.Init.WordLength   = UART_WORDLENGTH_8B;
    h_uart3.Init.StopBits     = UART_STOPBITS_1;
    h_uart3.Init.Parity       = UART_PARITY_NONE;
    h_uart3.Init.Mode         = UART_MODE_TX_RX;
    h_uart3.Init.HwFlowCtl   = UART_HWCONTROL_NONE;
    h_uart3.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&h_uart3);

    USART3->CR1 |= USART_CR1_RXNEIE;
    HAL_NVIC_SetPriority(USART3_IRQn, 3u, 3u);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
}

static void mon_uart_deinit(void)
{
    HAL_NVIC_DisableIRQ(USART3_IRQn);
    USART3->CR1 &= ~USART_CR1_RXNEIE;
    HAL_UART_DeInit(&h_uart3);
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_10 | GPIO_PIN_11);
    __HAL_RCC_USART3_CLK_DISABLE();
}

/* =========================================================================
 * TX: send KP / KI / AMP to motor controller
 * Format: #<kp>,<ki>,<amp>\n   e.g. #0.0054,0.0580,3.50\n
 * ======================================================================= */
static void mon_send_params(void)
{
    char buf[40];
    int n = snprintf(buf, sizeof(buf), "#%.4f,%.4f,%.2f\n",
                     (double)MS.kp, (double)MS.ki, (double)MS.amp);
    if (n > 0)
        HAL_UART_Transmit(&h_uart3, (uint8_t *)buf, (uint16_t)n, 100u);
}

/* =========================================================================
 * Push one sample into the circular buffer
 * ======================================================================= */
static void mon_push(float tgt, float act)
{
    int32_t next = (buf_head < 0) ? 0
                 : (int32_t)((buf_head + 1u) % MON_BUF_SIZE);
    buf_tgt[next] = tgt;
    buf_act[next] = act;
    buf_head = next;
    if (buf_count < MON_BUF_SIZE) buf_count++;
}

/* =========================================================================
 * Process a completed RX frame (called from main loop, not ISR)
 * ======================================================================= */
static void mon_process_uart(void)
{
    if (!frame_ready) return;
    frame_ready = 0u;
    float tgt = 0.0f, act = 0.0f;
    if (sscanf(frame_buf, "%f,%f", &tgt, &act) == 2) {
        mon_push(tgt, act);
        MS.last_tgt      = tgt;
        MS.last_act      = act;
        MS.last_rx_tick  = HAL_GetTick();
        MS.wave_dirty    = 1u;
        MS.info_dirty    = 1u;
    }
}

/* =========================================================================
 * Nice tick interval (targets ~5 visible ticks)
 * ======================================================================= */
static float compute_tick(float range)
{
    if (range <= 0.0f) return 1.0f;
    float rough = range / 5.0f;
    float mag   = powf(10.0f, floorf(log10f(rough)));
    float norm  = rough / mag;
    if (norm <= 1.5f) return mag;
    if (norm <= 3.5f) return 2.0f * mag;
    if (norm <= 7.5f) return 5.0f * mag;
    return 10.0f * mag;
}

/* =========================================================================
 * Draw [BACK] button (drawn once on full repaint)
 * ======================================================================= */
static void mon_draw_back(void)
{
    lcd_fill(BACK_X, BACK_Y, BACK_X + BACK_W - 1u, BACK_Y + BACK_H - 1u, C_BACK_BG);
    lcd_draw_rectangle(BACK_X, BACK_Y, BACK_X + BACK_W - 1u, BACK_Y + BACK_H - 1u, C_AXIS);
    /* "返回" = 2 Chinese chars = 32px; center in BACK_W=50 => offset=9 */
    lcd_show_chinese_str((uint16_t)(BACK_X + 9u), (uint16_t)(BACK_Y + 1u),
                         MON_STR_BACK, C_AXIS);
}

/* =========================================================================
 * Clear Y-label strip and draw dynamic rad/s labels at each tick line
 * ======================================================================= */
static void mon_draw_ylabels(void)
{
    lcd_fill(0u, OSC_TY, (uint16_t)(OSC_LX - 1u), OSC_TY + OSC_H - 1u, C_BG);

    float y_range = (float)OSC_H / MS.y_scale;
    float y_min   = MS.y_center - y_range * 0.5f;
    float y_max   = MS.y_center + y_range * 0.5f;
    float tick    = compute_tick(y_range);
    float first   = ceilf(y_min / tick) * tick;

    char lbl[8];
    for (float v = first; v <= y_max + tick * 0.01f; v += tick) {
        int py = (int)(OSC_TY + OSC_H / 2) - (int)((v - MS.y_center) * MS.y_scale);
        if (py < OSC_TY || py > OSC_TY + OSC_H - 1) continue;

        /* Keep label <= 5 chars to fit 40-px strip at 8px/char */
        if (fabsf(v) >= 100.0f)
            snprintf(lbl, sizeof(lbl), "%+.0f", (double)v);
        else if (fabsf(v) >= 10.0f)
            snprintf(lbl, sizeof(lbl), "%+.1f", (double)v);
        else
            snprintf(lbl, sizeof(lbl), "%+.2f", (double)v);

        int chars = (int)strlen(lbl);
        int lx    = OSC_LX - 3 - chars * 8;   /* right-align, 8px/char */
        if (lx < 0) lx = 0;
        lcd_show_string((uint16_t)lx, (uint16_t)(py - 8u),
                        (uint16_t)(OSC_LX - 3u), 16u, 16u, lbl, C_AXIS);
        lcd_draw_hline((uint16_t)(OSC_LX - 3u), (uint16_t)py, 3u, C_AXIS);
    }
}

/* =========================================================================
 * Draw grid + clear plot area  (called when wave_dirty)
 * ======================================================================= */
static void mon_draw_grid(void)
{
    lcd_fill(OSC_LX, OSC_TY, OSC_LX + OSC_W - 1u, OSC_TY + OSC_H - 1u, C_BG);

    /* Vertical grid lines */
    for (int c = 1; c < GRID_COLS; c++) {
        uint16_t x = (uint16_t)(OSC_LX + c * CELL_W);
        lcd_draw_line(x, OSC_TY, x, OSC_TY + OSC_H - 1u, C_GRID);
    }
    /* Horizontal grid lines */
    for (int r = 1; r < GRID_ROWS; r++) {
        uint16_t y = (uint16_t)(OSC_TY + r * CELL_H);
        lcd_draw_hline(OSC_LX, y, OSC_W, C_GRID);
    }

    /* Outer border */
    lcd_draw_rectangle(OSC_LX - 1u, OSC_TY - 1u,
                       OSC_LX + OSC_W - 1u, OSC_TY + OSC_H, C_AXIS);

    /* Zero-speed line */
    int py0 = (int)(OSC_TY + OSC_H / 2) - (int)(MS.y_center * MS.y_scale);
    if (py0 >= OSC_TY && py0 < OSC_TY + OSC_H)
        lcd_draw_hline(OSC_LX, (uint16_t)py0, OSC_W, C_ZERO);
}

/* =========================================================================
 * Draw waveforms (sliding window, newest sample on right)
 * ======================================================================= */
static void mon_draw_waveform(void)
{
    int32_t  head = buf_head;
    uint32_t cnt  = buf_count;
    if (head < 0 || cnt == 0u) {
        /* "等待串口数据" = 6 Chinese chars = 96px */
        lcd_show_chinese_str((uint16_t)(OSC_LX + 10u), (uint16_t)(OSC_TY + 70u),
                             MON_STR_WAIT, C_WARN);
        return;
    }

    int prev_pt = 0, prev_pa = 0;
    uint8_t have_prev = 0u;

    for (int x = 0; x < OSC_W; x++) {
        float dx        = (float)(OSC_W - 1 - x);
        int   sidx_back = (int)(dx / MS.x_scale + 0.5f);
        if ((uint32_t)sidx_back >= cnt) { have_prev = 0u; continue; }

        int32_t sidx = (head - sidx_back + (int32_t)MON_BUF_SIZE)
                       % (int32_t)MON_BUF_SIZE;
        float vt = buf_tgt[sidx];
        float va = buf_act[sidx];

        int py_t = (int)(OSC_TY + OSC_H / 2) - (int)((vt - MS.y_center) * MS.y_scale);
        int py_a = (int)(OSC_TY + OSC_H / 2) - (int)((va - MS.y_center) * MS.y_scale);

        if (py_t < OSC_TY)              py_t = OSC_TY;
        if (py_t > OSC_TY + OSC_H - 1) py_t = OSC_TY + OSC_H - 1;
        if (py_a < OSC_TY)              py_a = OSC_TY;
        if (py_a > OSC_TY + OSC_H - 1) py_a = OSC_TY + OSC_H - 1;

        uint16_t px = (uint16_t)(OSC_LX + x);
        if (have_prev) {
            uint16_t ppx = (uint16_t)(px - 1u);
            lcd_draw_line(ppx, (uint16_t)prev_pt, px, (uint16_t)py_t, C_TGT);
            lcd_draw_line(ppx, (uint16_t)prev_pa, px, (uint16_t)py_a, C_ACT);
        } else {
            lcd_draw_point(px, (uint16_t)py_t, C_TGT);
            lcd_draw_point(px, (uint16_t)py_a, C_ACT);
        }
        prev_pt = py_t; prev_pa = py_a; have_prev = 1u;
    }
}

/* =========================================================================
 * Draw one slider row  (label | track+fill | value)
 * ======================================================================= */
static void mon_draw_slider(uint16_t y, const uint8_t *label_cn,
                            float val, float vmin, float vmax,
                            const char *fmt, uint16_t fill_clr)
{
    /* Clear whole row */
    lcd_fill(0u, y, 239u, (uint16_t)(y + SLI_H - 1u), C_BG);

    /* Chinese label (left strip, 40px: 2 Chinese chars + ':' = 32+8=40px) */
    lcd_show_chinese_str(0u, (uint16_t)(y + 1u), label_cn, C_INFO);

    /* Track */
    lcd_fill((uint16_t)SLI_BX, (uint16_t)(y + 4u),
             (uint16_t)(SLI_BX + SLI_BW - 1u), (uint16_t)(y + SLI_H - 5u),
             C_SLI_TRK);

    /* Filled portion */
    float ratio = (val - vmin) / (vmax - vmin);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    uint16_t fill_w = (uint16_t)(ratio * SLI_BW);
    if (fill_w > 0u)
        lcd_fill((uint16_t)SLI_BX, (uint16_t)(y + 4u),
                 (uint16_t)(SLI_BX + fill_w - 1u), (uint16_t)(y + SLI_H - 5u),
                 fill_clr);

    /* Knob */
    uint16_t kx = (uint16_t)(SLI_BX + fill_w);
    if (kx > (uint16_t)(SLI_BX + SLI_BW - 3u)) kx = (uint16_t)(SLI_BX + SLI_BW - 3u);
    lcd_fill(kx, y, (uint16_t)(kx + 2u), (uint16_t)(y + SLI_H - 1u), C_AXIS);

    /* Value text (fixed-width so overwrite works without clearing) */
    char buf[10];
    snprintf(buf, sizeof(buf), fmt, (double)val);
    lcd_show_string((uint16_t)SLI_VX, (uint16_t)(y + 1u),
                    (uint16_t)(240u - SLI_VX), 16u, 16u, buf, C_AXIS);
}

/* =========================================================================
 * Draw all three sliders
 * ======================================================================= */
static void mon_draw_sliders(void)
{
    mon_draw_slider(SLI_KP_Y, MON_STR_KP,  MS.kp,  KP_MIN,  KP_MAX,  "%.4f ", C_SLI_KP);
    mon_draw_slider(SLI_KI_Y, MON_STR_KI,  MS.ki,  KI_MIN,  KI_MAX,  "%.4f ", C_SLI_KI);
    mon_draw_slider(SLI_AM_Y, MON_STR_AMP, MS.amp, AMP_MIN, AMP_MAX, "%.2f  ", C_SLI_AM);
}

/* =========================================================================
 * Draw / overwrite live values row (no clear needed - opaque text)
 * ======================================================================= */
static void mon_draw_livevals(void)
{
    char buf[16];
    /* Layout: "目标:" (40px) + value (8chars*8=64px) + "实际:" (40px) + value (64px) + "r/s" */
    /* 40+64+40+64 = 208px + "r/s"=24px -> total 232px, fits in 240px */
    lcd_show_chinese_str(0u,   INFO_VAL_Y, MON_STR_TGT_C, C_INFO);
    snprintf(buf, sizeof(buf), "%+8.3f", (double)MS.last_tgt);
    lcd_show_string(40u, INFO_VAL_Y, 64u, 16u, 16u, buf, C_INFO);

    lcd_show_chinese_str(104u, INFO_VAL_Y, MON_STR_ACT_C, C_INFO);
    snprintf(buf, sizeof(buf), "%+8.3f", (double)MS.last_act);
    lcd_show_string(144u, INFO_VAL_Y, 64u, 16u, 16u, buf, C_INFO);

    lcd_show_string(208u, INFO_VAL_Y, 32u, 16u, 16u, "r/s", C_INFO);
}

/* =========================================================================
 * Draw / overwrite current-params row
 * ======================================================================= */
static void mon_draw_params(void)
{
    /* Fixed-width string: "P:0.0054 I:0.0580 K: 1.00 " = 26 chars = 208px  */
    char line[32];
    snprintf(line, sizeof(line), "P:%.4f I:%.4f K:%5.2f ",
             (double)MS.kp, (double)MS.ki, (double)MS.amp);
    lcd_show_string(0u, INFO_PAR_Y, 240u, 16u, 16u, line, C_INFO);
}

/* =========================================================================
 * Draw / overwrite connection status (no clear)
 * ======================================================================= */
static void mon_draw_status(void)
{
    uint32_t now = HAL_GetTick();
    const uint8_t *sta; uint16_t clr;
    if (buf_head < 0) {
        sta = MON_STR_WAIT; clr = C_WARN;  /* "等待串口数据" */
    } else if (now - MS.last_rx_tick > CONN_TIMEOUT_MS) {
        sta = MON_STR_TOUT; clr = C_WARN;  /* "串口超时 检查接线" */
    } else {
        sta = MON_STR_OK;   clr = C_OK;    /* "已连接 接收正常" */
    }
    /* Clear status row first (different messages have different widths) */
    lcd_fill(0u, INFO_STA_Y, 239u, (uint16_t)(INFO_STA_Y + 15u), C_BG);
    lcd_show_chinese_str(0u, INFO_STA_Y, sta, clr);
}

/* =========================================================================
 * Full screen repaint  (called once on entry)
 * ======================================================================= */
static void mon_draw_full(void)
{
    g_back_color = C_BG;   /* KEY FIX: all text backgrounds = black */
    lcd_clear(C_BG);

    /*  Header row  (y=0..24, 25px tall)  -- no overlaps:
     *  x=  0.. 63  "电机监控"  (4ch * 16 = 64px)
     *  x= 68.. 79  yellow swatch (12 * 6px)
     *  x= 80..111  "目标"       (2ch * 16 = 32px)
     *  x=116..127  cyan swatch  (12 * 6px)
     *  x=128..159  "实际"       (2ch * 16 = 32px)
     *  x=188..237  [返回] BACK button (50px)
     */
    lcd_show_chinese_str(0u,   4u, MON_STR_TITLE, C_TITLE);
    lcd_fill(68u,  9u,  79u,  14u, C_TGT);  /* yellow swatch */
    lcd_show_chinese_str(80u,  4u, MON_STR_TGT, C_TGT);
    lcd_fill(116u, 9u,  127u, 14u, C_ACT);  /* cyan swatch */
    lcd_show_chinese_str(128u, 4u, MON_STR_ACT, C_ACT);
    mon_draw_back();

    mon_draw_grid();
    mon_draw_ylabels();
    mon_draw_sliders();
    mon_draw_livevals();
    mon_draw_params();
    mon_draw_status();

    /* Static UART info (drawn once, never changes) */
    lcd_show_string(0u, INFO_UART_Y, 240u, 16u, 16u,
                    "串口3 PB10发 PB11收 115200", C_INFO);
}

/* =========================================================================
 * Touch handler
 * Returns 255 if [BACK] touched, 0 otherwise.
 * Also updates sliders on drag.
 * ======================================================================= */
static uint8_t mon_touch(void)
{
    tp_dev.scan(0);
    if (!(tp_dev.sta & TP_PRES_DOWN)) return 0u;

    uint16_t tx = tp_dev.x[0];
    uint16_t ty = tp_dev.y[0];

    /* [BACK] button */
    if (tx >= BACK_X && tx < (uint16_t)(BACK_X + BACK_W) &&
        ty >= BACK_Y && ty < (uint16_t)(BACK_Y + BACK_H)) {
        delay_ms(80u);
        return 255u;
    }

    /* Slider drag: must be within bar x range */
    if (tx >= SLI_BX && tx < (uint16_t)(SLI_BX + SLI_BW)) {
        float pos = (float)(tx - SLI_BX) / (float)SLI_BW;
        if (pos < 0.0f) pos = 0.0f;
        if (pos > 1.0f) pos = 1.0f;

        if (ty >= SLI_KP_Y && ty < (uint16_t)(SLI_KP_Y + SLI_H)) {
            float nv = KP_MIN + pos * (KP_MAX - KP_MIN);
            if (nv != MS.kp) { MS.kp = nv; MS.ctrl_dirty = 1u; MS.tx_pending = 1u; }
        } else if (ty >= SLI_KI_Y && ty < (uint16_t)(SLI_KI_Y + SLI_H)) {
            float nv = KI_MIN + pos * (KI_MAX - KI_MIN);
            if (nv != MS.ki) { MS.ki = nv; MS.ctrl_dirty = 1u; MS.tx_pending = 1u; }
        } else if (ty >= SLI_AM_Y && ty < (uint16_t)(SLI_AM_Y + SLI_H)) {
            float nv = AMP_MIN + pos * (AMP_MAX - AMP_MIN);
            if (nv != MS.amp) { MS.amp = nv; MS.ctrl_dirty = 1u; MS.tx_pending = 1u; }
        }
    }
    return 0u;
}

/* =========================================================================
 * Key hold handler - continuous smooth zoom (direct GPIO, no debounce)
 * ======================================================================= */
static void mon_handle_keys(void)
{
    if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4) == GPIO_PIN_RESET) {
        MS.x_scale *= ZOOM_K;
        if (MS.x_scale > XSCALE_MAX) MS.x_scale = XSCALE_MAX;
        MS.wave_dirty = 1u;
    }
    if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_2) == GPIO_PIN_RESET) {
        MS.x_scale /= ZOOM_K;
        if (MS.x_scale < XSCALE_MIN) MS.x_scale = XSCALE_MIN;
        MS.wave_dirty = 1u;
    }
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET) {
        MS.y_scale *= ZOOM_K;
        if (MS.y_scale > YSCALE_MAX) MS.y_scale = YSCALE_MAX;
        MS.wave_dirty = 1u;
    }
    if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3) == GPIO_PIN_RESET) {
        MS.y_scale /= ZOOM_K;
        if (MS.y_scale < YSCALE_MIN) MS.y_scale = YSCALE_MIN;
        MS.wave_dirty = 1u;
    }
}

/* =========================================================================
 * Entry point
 * ======================================================================= */
void motor_monitor_run(void)
{
    /* Reset state */
    MS.x_scale       = XSCALE_DEF;
    MS.y_scale       = YSCALE_DEF;
    MS.y_center      = 0.0f;
    MS.kp            = KP_DEF;
    MS.ki            = KI_DEF;
    MS.amp           = AMP_DEF;
    MS.last_tgt      = 0.0f;
    MS.last_act      = 0.0f;
    MS.last_rx_tick  = HAL_GetTick();
    MS.last_tx_tick  = 0u;
    MS.last_info_tick= 0u;
    MS.wave_dirty    = 0u;
    MS.ctrl_dirty    = 0u;
    MS.info_dirty    = 0u;
    MS.tx_pending    = 1u;  /* send defaults immediately */
    buf_head         = -1;
    buf_count        = 0u;
    frame_ready      = 0u;
    rx_idx           = 0u;

    mon_uart_init();
    mon_draw_full();

    uint32_t frame_t0 = HAL_GetTick();

    while (1) {
        /* 1. Parse incoming UART frame */
        mon_process_uart();

        /* 2. Touch: sliders + BACK */
        if (mon_touch() == 255u) {
            mon_uart_deinit();
            lcd_clear(C_BG);
            return;
        }

        /* 3. Key zoom (held) */
        mon_handle_keys();

        uint32_t now = HAL_GetTick();

        /* 4. Frame-rate-limited selective redraws */
        if (now - frame_t0 >= (uint32_t)FRAME_MS) {
            frame_t0 = now;

            /* Plot: only when new data or zoom changed */
            if (MS.wave_dirty) {
                MS.wave_dirty = 0u;
                mon_draw_grid();
                mon_draw_ylabels();
                mon_draw_waveform();
            }

            /* Sliders + params: only when slider dragged */
            if (MS.ctrl_dirty) {
                MS.ctrl_dirty = 0u;
                mon_draw_sliders();
                mon_draw_params();
            }

            /* Live values + status: update when new sample OR once/second */
            if (MS.info_dirty || (now - MS.last_info_tick > 1000u)) {
                MS.info_dirty      = 0u;
                MS.last_info_tick  = now;
                mon_draw_livevals();
                mon_draw_status();
            }
        }

        /* 5. TX: send params when pending, throttled to TX_MIN_MS */
        if (MS.tx_pending && (now - MS.last_tx_tick > (uint32_t)TX_MIN_MS)) {
            MS.tx_pending    = 0u;
            MS.last_tx_tick  = now;
            mon_send_params();
        }

        delay_ms(5u);
    }
}
