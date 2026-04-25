/**
 * @file  oscilloscope.c
 * @brief High-precision weak DC voltage measurement system (0~100 mV).
 *        Re-uses the full oscilloscope display framework (grid, waveform,
 *        FFT, sliders, info-bar) with ADC real data replacing math generation.
 *
 * Layout (portrait 240x320) -- UNCHANGED from oscilloscope framework:
 *   y=  0.. 24  Top bar: [返回][校零][均值][IIR][中值][FFT/趋势]
 *   y= 25..224  10x10 oscilloscope grid (200x200 px), shows voltage TREND
 *   y=225..247  Digital readout line 1 (large)
 *   y=248..270  Digital readout line 2 (status)
 *   y=273..287  Info bar  (V/div, filter, sample rate)
 *   y=290..319  Key-hint bar
 *
 * Key controls:
 *   KEY0  (PE4, active-low) : auto-zero calibration (short input first)
 *   KEY2  (PE2, active-low) : cycle filter mode (AVG -> IIR -> MEDIAN -> AVG)
 *   KEY_UP(PA0, active-high): zoom out (increase V/div + raise v_center)
 *   KEY1  (PE3, active-low) : zoom in  (decrease V/div + lower v_center)
 *
 * Measurement chain:
 *   PA5 (ADC1_IN5) <- LM358P gain-21 amp <- RC LP (10k+1uF) <- Vin
 *   ADC: TIM3 TRGO @ 10 kHz, 480-cycle sampling, DMA2 circular 1000-sample buffer
 *   1000 samples = 100 ms = exactly 5 periods of 50 Hz -> perfect 50Hz rejection
 *
 * Filter options:
 *   0-AVG  : mean of all 1000 DMA samples (synchronized 50Hz cancellation)
 *   1-IIR  : exponential moving average (alpha=0.1) on each batch mean
 *   2-MED  : median of last 3 batch means (outlier rejection)
 *
 * All comments pure ASCII (Keil GBK-safe).
 */

#include "oscilloscope.h"
#include "chinese_font.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/KEY/key.h"
#include "./BSP/TOUCH/touch.h"
#include "./BSP/ADC/adc_dc.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ASCII labels (no Chinese -- avoids GBK garbling) */
static const char LBL_BACK[]  = "BACK";
static const char LBL_ZERO[]  = "ZERO";
static const char LBL_AVG[]   = "AVG";
static const char LBL_IIR[]   = "IIR";
static const char LBL_MED[]   = "MED";
static const char LBL_FFT[]   = "FREQ";
static const char LBL_TREND[] = "TIME";
static const char LBL_HINT[]  = "K0:ZERO  K2:FILT  UP:+  K1:-";

#ifndef M_PI
#define M_PI 3.14159265358979f
#endif

/* ===========================================================================
 * Colour palette (RGB-565) -- UNCHANGED
 * =========================================================================*/
#define C_SCR      0x0008
#define C_GRD_BG   0x0000
#define C_GRD_MJ   0x2104
#define C_AXIS     0x4A69
#define C_TICK     0x3186
#define C_WAVE     0xFFE0   /* yellow trend line */
#define C_WAVE_OVR 0xF800   /* red: over-range sample */
#define C_FFT_BAR  0x07E0
#define C_FFT_PK   0xF800
#define C_TXT      0xFFFF
#define C_TXT_DIM  0x8410
#define C_BTN_OFF  0x39E7
#define C_INFO_BG  0x10A2
#define C_RDO_BG   0x0841   /* readout area background */
#define C_GREEN    0x07E0
#define C_RED      0xF800
#define C_YELLOW   0xFFE0
#define C_ORANGE   0xFD20

static const uint16_t BTN_ACT[6] = {
    0xC340,   /* BACK  */
    0x035F,   /* ZERO  - blue  */
    0x07C0,   /* AVG   - green */
    0xFC40,   /* IIR   - orange */
    0x780F,   /* MED   - purple */
    0xFFE0,   /* FFT   - yellow */
};

/* ===========================================================================
 * Grid / display geometry -- UNCHANGED
 * =========================================================================*/
#define GRID_COLS  10
#define GRID_ROWS  10
#define CELL_W     20
#define CELL_H     20
#define OSC_LX     25
#define OSC_TY     25
#define OSC_W      (GRID_COLS * CELL_W)    /* 200 */
#define OSC_H      (GRID_ROWS * CELL_H)    /* 200 */
#define OSC_CX     (OSC_LX + OSC_W / 2)   /* 125 */
#define OSC_CY     (OSC_TY + OSC_H / 2)   /* 125 */
#define BAR_H      25

/* Readout rows (replaces slider rows) */
#define RDO_Y1     232     /* large voltage readout centre */
#define RDO_Y2     258     /* status line centre */

/* Info / hint rows */
#define INFO_Y     273
#define HINT_Y     292

/* ===========================================================================
 * Signal / FFT parameters -- UNCHANGED
 * =========================================================================*/
#define N_SAMPLES  200
#define N_FFT      256
#define ZOOM_K     1.259f   /* x10 per 6 steps (one per key event) */
#define FRAME_MS   100u     /* 10 Hz frame rate (ADC updates every 100 ms) */

/* ===========================================================================
 * Filter modes
 * =========================================================================*/
#define FILT_AVG   0u   /* synchronized 50Hz averaging */
#define FILT_IIR   1u   /* exponential moving average */
#define FILT_MED   2u   /* median of 3 readings */

/* V/div preset table (V) */
static const float VDIV_TABLE[] = {
    0.001f, 0.002f, 0.005f, 0.010f, 0.020f, 0.050f, 0.100f, 0.200f
};
#define VDIV_STEPS  8u
#define VDIV_DEF_IDX  4u    /* 0.020 V = 20 mV/div -> 200mV range (good for 0-100mV) */

/* ===========================================================================
 * State
 * =========================================================================*/
typedef struct {
    float    v_per_div;     /* V/div for display */
    float    v_center;      /* centre voltage of grid (V) */
    uint8_t  fft_mode;      /* 0=trend, 1=FFT of trend */
    uint8_t  filter_mode;   /* FILT_AVG / FILT_IIR / FILT_MED */
    uint8_t  vdiv_idx;      /* index into VDIV_TABLE */
    /* Calibration */
    float    v_zero;        /* zero offset subtracted from each reading (V) */
    uint8_t  calibrated;    /* 1 after auto-zero performed */
    /* Live reading */
    float    v_now;         /* latest calibrated voltage (V) */
    uint8_t  over_range;    /* 1 if v_now > ADC_DC_FULLSCALE */
    /* IIR state */
    float    iir_acc;
    uint8_t  iir_init;
    /* Median buffer */
    float    med_buf[3];
    uint8_t  med_idx;
    /* Edge detection */
    uint8_t  prev_touch;
    /* Key edge detection */
    uint8_t  k0_prev;
    uint8_t  k2_prev;
} DcMeterState;

static DcMeterState  S;
static float         g_samp[N_SAMPLES];   /* trend: last N voltage readings */
static float         g_re[N_FFT];
static float         g_im[N_FFT];

/* ===========================================================================
 * Utility -- UNCHANGED
 * =========================================================================*/
static float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/* ===========================================================================
 * Signal processing
 * =========================================================================*/

/* Compute mean of DMA buffer and return calibrated input voltage (V) */
static float compute_raw_voltage(void)
{
    float v_pa5 = adc_dc_mean_pa5();
    return v_pa5 / ADC_DC_HW_GAIN;   /* referred to input */
}

static float filter_apply(float raw_v)
{
    switch (S.filter_mode)
    {
        default:
        case FILT_AVG:
            return raw_v;    /* averaging is already done in adc_dc_mean_pa5 */

        case FILT_IIR:
        {
            float alpha = 0.10f;    /* smoothing: higher = faster but noisier */
            if (!S.iir_init) {
                S.iir_acc  = raw_v;
                S.iir_init = 1u;
            }
            S.iir_acc = S.iir_acc * (1.0f - alpha) + raw_v * alpha;
            return S.iir_acc;
        }

        case FILT_MED:
        {
            S.med_buf[S.med_idx] = raw_v;
            S.med_idx = (uint8_t)((S.med_idx + 1u) % 3u);
            float a = S.med_buf[0], b = S.med_buf[1], c = S.med_buf[2];
            /* simple 3-element sort */
            float lo  = (a < b) ? a : b;
            float hi  = (a > b) ? a : b;
            float med = (c < lo) ? lo : (c > hi) ? hi : c;
            return med;
        }
    }
}

/* Update trend buffer: shift left, append new reading */
static void trend_push(float v)
{
    for (int i = 0; i < N_SAMPLES - 1; i++)
        g_samp[i] = g_samp[i + 1];
    g_samp[N_SAMPLES - 1] = v;
}

/* Called every FRAME_MS; reads ADC when ready, applies filter, updates trend */
static void gen_samples(void)
{
    if (!g_adc_dc_ready) return;
    g_adc_dc_ready = 0u;

    float raw   = compute_raw_voltage();
    float filt  = filter_apply(raw);
    float calib = filt - S.v_zero;
    /* Do NOT hard-clamp to 0: a large negative result means v_zero was recorded
     * while PA5 was floating.  Allow a small negative floor so the anomaly is
     * visible on screen rather than silently frozen at 0.                     */
    if (calib < -ADC_DC_FULLSCALE * 0.05f) calib = -ADC_DC_FULLSCALE * 0.05f;

    S.v_now      = calib;
    S.over_range = (calib > ADC_DC_FULLSCALE * 1.05f) ? 1u : 0u;
    trend_push(calib);
}

/* ===========================================================================
 * FFT -- UNCHANGED
 * =========================================================================*/
static void do_fft(void)
{
    for (int i = 0; i < N_FFT; i++) {
        if (i < N_SAMPLES) {
            float w = 0.5f * (1.0f - cosf(2.0f * M_PI * (float)i
                                           / (float)(N_SAMPLES - 1)));
            g_re[i] = g_samp[i] * w;
        } else {
            g_re[i] = 0.0f;
        }
        g_im[i] = 0.0f;
    }
    for (int i = 1, j = 0; i < N_FFT; i++) {
        int bit = N_FFT >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t;
            t = g_re[i]; g_re[i] = g_re[j]; g_re[j] = t;
            t = g_im[i]; g_im[i] = g_im[j]; g_im[j] = t;
        }
    }
    for (int len = 2; len <= N_FFT; len <<= 1) {
        float ang  = -2.0f * M_PI / (float)len;
        float wbRe = cosf(ang), wbIm = sinf(ang);
        for (int i = 0; i < N_FFT; i += len) {
            float cRe = 1.0f, cIm = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                float uR = g_re[i+k], uI = g_im[i+k];
                float vR = g_re[i+k+len/2]*cRe - g_im[i+k+len/2]*cIm;
                float vI = g_re[i+k+len/2]*cIm + g_im[i+k+len/2]*cRe;
                g_re[i+k]       = uR+vR; g_im[i+k]       = uI+vI;
                g_re[i+k+len/2] = uR-vR; g_im[i+k+len/2] = uI-vI;
                float nRe = cRe*wbRe - cIm*wbIm;
                cIm = cRe*wbIm + cIm*wbRe; cRe = nRe;
            }
        }
    }
}

/* ===========================================================================
 * Top button bar
 * =========================================================================*/
static const char * const BTN_LBL[6] = {
    LBL_BACK, LBL_ZERO, LBL_AVG, LBL_IIR, LBL_MED, LBL_FFT
};

static void draw_topbar(void)
{
    uint16_t bw = (uint16_t)(lcddev.width / 6u);
    for (int i = 0; i < 6; i++) {
        uint16_t x0   = (uint16_t)(i * bw);
        uint16_t x1   = (i == 5) ? (uint16_t)(lcddev.width - 1u)
                                  : (uint16_t)(x0 + bw - 1u);
        uint16_t btnw = (uint16_t)(x1 - x0 + 1u);
        uint8_t  active = 0u;
        if (i == 2 && S.filter_mode == FILT_AVG) active = 1u;
        if (i == 3 && S.filter_mode == FILT_IIR) active = 1u;
        if (i == 4 && S.filter_mode == FILT_MED) active = 1u;
        if (i == 5 && S.fft_mode)                active = 1u;
        uint16_t bg = active ? BTN_ACT[i] : C_BTN_OFF;
        lcd_fill(x0, 0u, x1, BAR_H - 1u, bg);
        lcd_draw_rectangle(x0, 0u, x1, BAR_H - 1u, C_TXT_DIM);
        const char *lbl = (i == 5u && !S.fft_mode) ? LBL_TREND : BTN_LBL[i];
        uint16_t lw  = (uint16_t)(strlen(lbl) * 8u);
        uint16_t tx  = x0 + (btnw > lw ? (btnw - lw) / 2u : 0u);
        uint16_t ty  = (uint16_t)((BAR_H - 12u) / 2u);
        lcd_show_string(tx, ty, btnw, BAR_H, 12u, (char *)lbl,
                        active ? C_TXT : C_TXT_DIM);
    }
}

/* ===========================================================================
 * Oscilloscope grid -- UNCHANGED
 * =========================================================================*/
static void draw_grid(void)
{
    lcd_fill(OSC_LX, OSC_TY, OSC_LX + OSC_W - 1, OSC_TY + OSC_H - 1, C_GRD_BG);
    for (int r = 0; r <= GRID_ROWS; r++) {
        uint16_t y = (uint16_t)(OSC_TY + r * CELL_H);
        uint16_t c = (r == GRID_ROWS / 2) ? C_AXIS : C_GRD_MJ;
        lcd_draw_hline(OSC_LX, y, OSC_W, c);
    }
    for (int col = 0; col <= GRID_COLS; col++) {
        uint16_t x = (uint16_t)(OSC_LX + col * CELL_W);
        uint16_t c = (col == GRID_COLS / 2) ? C_AXIS : C_GRD_MJ;
        lcd_draw_line(x, OSC_TY, x, OSC_TY + OSC_H, c);
    }
    for (int sub = 1; sub < GRID_COLS * 5; sub++) {
        uint16_t xs = (uint16_t)(OSC_LX + sub * CELL_W / 5);
        if (xs >= OSC_LX + OSC_W) break;
        lcd_draw_point(xs, OSC_CY - 2, C_TICK);
        lcd_draw_point(xs, OSC_CY + 2, C_TICK);
    }
    for (int sub = 1; sub < GRID_ROWS * 5; sub++) {
        uint16_t ys = (uint16_t)(OSC_TY + sub * CELL_H / 5);
        if (ys >= OSC_TY + OSC_H) break;
        lcd_draw_point(OSC_CX - 2, ys, C_TICK);
        lcd_draw_point(OSC_CX + 2, ys, C_TICK);
    }
}

/* ===========================================================================
 * Y-axis voltage labels (absolute mV/V values, offset by v_center)
 * FRAMEWORK UNCHANGED; only the label value formula now includes v_center.
 * =========================================================================*/
static void draw_ylabels(void)
{
    char buf[10];
    lcd_fill(0, OSC_TY, OSC_LX - 1, OSC_TY + OSC_H - 1, C_SCR);
    static const int ROWS[] = {1, 3, 5, 7, 9};
    for (int ri = 0; ri < 5; ri++) {
        int    r  = ROWS[ri];
        /* Absolute voltage at this row: centre + offset */
        float  v  = S.v_center + ((float)(GRID_ROWS / 2) - (float)r) * S.v_per_div;
        int16_t ly = (int16_t)(OSC_TY + r * CELL_H) - 6;
        if (ly < (int16_t)OSC_TY)                    ly = (int16_t)OSC_TY;
        if (ly + 12 > (int16_t)(OSC_TY + OSC_H))     continue;
        /* Format in mV for sub-100mV display */
        if (fabsf(v) < 0.001f)
            snprintf(buf, sizeof(buf), "0mV");
        else if (fabsf(v) < 1.0f)
            snprintf(buf, sizeof(buf), "%.0fmV", v * 1000.0f);
        else
            snprintf(buf, sizeof(buf), "%.2fV", v);
        uint16_t tc = (r == GRID_ROWS / 2) ? C_AXIS : C_TXT_DIM;
        lcd_show_string(0, (uint16_t)ly, OSC_LX, 12u, 12u, buf, tc);
    }
}

/* ===========================================================================
 * Waveform drawing (time-domain trend)
 * FRAMEWORK UNCHANGED; added v_center offset and over-range colour.
 * =========================================================================*/
static void draw_waveform(void)
{
    float pxv = (float)CELL_H / S.v_per_div;   /* pixels per volt */
    int16_t prev_px = -1, prev_py = -1;

    for (int i = 0; i < N_SAMPLES; i++) {
        int16_t px = (int16_t)(OSC_LX + (int32_t)i * (OSC_W - 1) / (N_SAMPLES - 1));
        /* Map voltage relative to v_center */
        int16_t py = (int16_t)(OSC_CY - (g_samp[i] - S.v_center) * pxv + 0.5f);
        if (py < (int16_t)OSC_TY)               py = (int16_t)OSC_TY;
        if (py > (int16_t)(OSC_TY + OSC_H - 1)) py = (int16_t)(OSC_TY + OSC_H - 1);

        uint16_t color = (g_samp[i] > ADC_DC_FULLSCALE * 1.05f) ? C_WAVE_OVR : C_WAVE;

        if (prev_px >= 0) {
            lcd_draw_line((uint16_t)prev_px, (uint16_t)prev_py,
                          (uint16_t)px,      (uint16_t)py, color);
        }
        prev_px = px;
        prev_py = py;
    }
}

/* ===========================================================================
 * FFT spectrum -- UNCHANGED (operates on g_samp[] trend data)
 * =========================================================================*/
static void draw_fft_spectrum(void)
{
    do_fft();
    int   nbins = N_FFT / 2;
    float peak_mag = 0.0f;
    int   peak_bin = 1;
    for (int k = 1; k < nbins; k++) {
        float m = sqrtf(g_re[k]*g_re[k] + g_im[k]*g_im[k]);
        if (m > peak_mag) { peak_mag = m; peak_bin = k; }
    }
    if (peak_mag < 1e-9f) peak_mag = 1e-9f;
    for (int x = 0; x < OSC_W; x++) {
        int   k   = (int)((float)x * (float)nbins / (float)OSC_W);
        if (k >= nbins) k = nbins - 1;
        float mag = sqrtf(g_re[k]*g_re[k] + g_im[k]*g_im[k]) / peak_mag;
        int16_t bh = (int16_t)(mag * (float)(OSC_H - 10) + 0.5f);
        if (bh < 0) bh = 0;
        if (bh > OSC_H - 10) bh = OSC_H - 10;
        uint16_t sx = (uint16_t)(OSC_LX + x);
        uint16_t y1 = (uint16_t)(OSC_TY + OSC_H - 1 - bh);
        uint16_t y2 = (uint16_t)(OSC_TY + OSC_H - 1);
        uint16_t fc = (k == peak_bin) ? C_FFT_PK : C_FFT_BAR;
        if (bh > 0) lcd_draw_line(sx, y1, sx, y2, fc);
    }
    /* Frequency of peak (trend update rate = 10Hz, N_SAMPLES=200) */
    float pfreq = (float)peak_bin * 10.0f / (float)N_FFT;
    char  buf[20];
    snprintf(buf, sizeof(buf), "%.2fHz", pfreq);
    lcd_fill(OSC_LX+2, OSC_TY+2, OSC_LX+150, OSC_TY+18, C_GRD_BG);
    lcd_show_string(OSC_LX+4, OSC_TY+5, 150u, 12u, 12u, buf, C_FFT_PK);
}

/* ===========================================================================
 * Digital readout (replaces slider area)
 * =========================================================================*/
static void draw_readout(void)
{
    char line1[24], line2[32];

    /* Line 1: large voltage value */
    lcd_fill(0u, 225u, 239u, 246u, C_RDO_BG);
    if (S.over_range) {
        snprintf(line1, sizeof(line1), "OVER RANGE !");
        lcd_show_string(20u, 228u, 200u, 16u, 16u, line1, C_RED);
    } else {
        /* Display as X.XXXX V (0.0000 V format, 4 decimal places) */
        snprintf(line1, sizeof(line1), "%7.4f V", (double)S.v_now);
        uint16_t vc = S.calibrated ? C_GREEN : C_YELLOW;
        lcd_show_string(12u, 228u, 216u, 16u, 16u, line1, vc);
    }

    /* Line 2: filter mode + calibration status */
    lcd_fill(0u, 247u, 239u, 270u, C_RDO_BG);
    const char *filt_name =
        (S.filter_mode == FILT_AVG) ? "AVG " :
        (S.filter_mode == FILT_IIR) ? "IIR " : "MED ";
    const char *cal_str  = S.calibrated ? "CAL:OK" : "CAL:--";
    snprintf(line2, sizeof(line2), "Filt:%-4s  %s  %s",
             filt_name, cal_str, S.over_range ? "[OVER]" : "      ");
    lcd_show_string(4u, 254u, 232u, 12u, 12u, line2, C_TXT_DIM);
}

/* ===========================================================================
 * Info bar
 * =========================================================================*/
static void draw_infobar(void)
{
    char buf[36];
    lcd_fill(0u, INFO_Y - 1u, lcddev.width - 1u, INFO_Y + 13u, C_INFO_BG);
    const char *filt_name =
        (S.filter_mode == FILT_AVG) ? "AVG" :
        (S.filter_mode == FILT_IIR) ? "IIR" : "MED";
    snprintf(buf, sizeof(buf), "%.0fmV/d  Fs:10k  F:%s  T:%.0fs",
             S.v_per_div * 1000.0f,
             filt_name,
             (double)(N_SAMPLES * FRAME_MS) / 1000.0);
    lcd_show_string(0u, INFO_Y, 240u, 12u, 12u, buf, C_TXT_DIM);
}

/* ===========================================================================
 * Key-hint bar (drawn once)
 * =========================================================================*/
static void draw_keyhint(void)
{
    lcd_fill(0u, HINT_Y, lcddev.width - 1u, lcddev.height - 1u, C_SCR);
    lcd_show_string(0u, HINT_Y, lcddev.width, 16u, 12u, (char *)LBL_HINT, C_TXT_DIM);
}

/* ===========================================================================
 * Full screen repaint -- UNCHANGED structure
 * =========================================================================*/
static void full_redraw(void)
{
    draw_topbar();
    draw_grid();
    draw_ylabels();
    if (S.fft_mode) draw_fft_spectrum(); else draw_waveform();
    draw_readout();
    draw_infobar();
}

/* ===========================================================================
 * Auto-zero calibration
 * After calling, input must be shorted (0 V).
 * Averages 5 DMA batches to get stable zero offset.
 * =========================================================================*/
/* Maximum credible zero offset (input-referred).
 * If the measured offset exceeds this the input is likely floating /
 * disconnected -- reject and warn instead of recording a garbage v_zero.
 * Half of full-scale (50 mV for a 100 mV system) is a safe bound. */
#define ZERO_SANITY_MAX   (ADC_DC_FULLSCALE * 0.5f)

static void do_auto_zero(void)
{
    /* Wait for up to 1 second for 5 fresh ADC batches */
    float acc = 0.0f;
    uint8_t cnt = 0u;
    uint32_t t0 = HAL_GetTick();
    while (cnt < 5u && (HAL_GetTick() - t0) < 1000u) {
        if (g_adc_dc_ready) {
            g_adc_dc_ready = 0u;
            acc += adc_dc_mean_pa5() / ADC_DC_HW_GAIN;
            cnt++;
        }
    }
    if (cnt > 0u) {
        float candidate = acc / (float)cnt;

        /* Sanity check: if offset is unreasonably large the input is almost
         * certainly floating (PA5 disconnected).  Reject the calibration and
         * show a visible error -- do NOT overwrite the previous v_zero.       */
        if (candidate > ZERO_SANITY_MAX || candidate < -ZERO_SANITY_MAX) {
            uint16_t bw = (uint16_t)(lcddev.width / 6u);
            uint16_t z0 = bw, z1 = (uint16_t)(2u * bw - 1u);
            lcd_fill(z0, 0u, z1, BAR_H - 1u, C_RED);
            lcd_show_string(z0, (uint16_t)((BAR_H - 12u) / 2u),
                            bw, BAR_H, 12u, "ERR", C_TXT);
            lcd_fill(0u, 247u, 239u, 270u, C_RDO_BG);
            lcd_show_string(4u, 254u, 232u, 12u, 12u,
                            "ZERO ERR:short input GND first", C_RED);
            delay_ms(2000u);
            return;   /* keep old v_zero intact */
        }

        S.v_zero      = candidate;
        S.calibrated  = 1u;
        S.iir_acc     = 0.0f;
        S.iir_init    = 0u;
    }
}

/* ===========================================================================
 * V/div adjustment
 * =========================================================================*/
static void vdiv_increase(void)
{
    if (S.vdiv_idx < VDIV_STEPS - 1u) {
        S.vdiv_idx++;
        S.v_per_div = VDIV_TABLE[S.vdiv_idx];
        S.v_center  = S.v_per_div * (float)(GRID_ROWS / 2); /* re-centre */
        if (S.v_center < 0.0f) S.v_center = 0.0f;
    }
}

static void vdiv_decrease(void)
{
    if (S.vdiv_idx > 0u) {
        S.vdiv_idx--;
        S.v_per_div = VDIV_TABLE[S.vdiv_idx];
        S.v_center  = S.v_per_div * (float)(GRID_ROWS / 2);
        if (S.v_center < 0.0f) S.v_center = 0.0f;
    }
}

/* ===========================================================================
 * Touch input handler -- framework UNCHANGED; button meanings updated
 * Returns 0=none, 1=redraw, 255=BACK
 * =========================================================================*/
static uint8_t handle_touch(void)
{
    tp_dev.scan(0);
    uint8_t now  = (tp_dev.sta & TP_PRES_DOWN) ? 1u : 0u;
    uint8_t dn   = now && !S.prev_touch;
    S.prev_touch = now;
    if (!now || !dn) return 0u;

    uint16_t tx = tp_dev.x[0];
    uint16_t ty = tp_dev.y[0];

    if (ty < BAR_H) {
        uint16_t bw  = (uint16_t)(lcddev.width / 6u);
        int      btn = (int)(tx / bw);
        if (btn > 5) btn = 5;
        switch (btn) {
            case 0: return 255u;                        /* [返回] BACK */
            case 1:                                     /* [ZERO] auto-zero */
            {
                /* Flash button blue to confirm the press was registered */
                uint16_t z0 = bw, z1 = (uint16_t)(2u * bw - 1u);
                lcd_fill(z0, 0u, z1, BAR_H - 1u, BTN_ACT[1]);
                lcd_show_string(z0, (uint16_t)((BAR_H - 12u) / 2u),
                                bw, BAR_H, 12u, "...", C_TXT);
                do_auto_zero();
                S.prev_touch = 0u;  /* clear so lift/re-press is detected cleanly */
                return 1u;
            }
            case 2: S.filter_mode = FILT_AVG; return 1u;
            case 3: S.filter_mode = FILT_IIR; S.iir_init=0u; return 1u;
            case 4: S.filter_mode = FILT_MED; S.med_idx=0u; return 1u;
            case 5: S.fft_mode ^= 1u;         return 1u;
        }
    }
    return 0u;
}

/* ===========================================================================
 * Key input handler -- framework UNCHANGED; key meanings updated
 * =========================================================================*/
static uint8_t handle_keys(void)
{
    uint8_t k0  = (HAL_GPIO_ReadPin(KEY0_GPIO_PORT, KEY0_GPIO_PIN) == GPIO_PIN_RESET);
    uint8_t k1  = (HAL_GPIO_ReadPin(KEY1_GPIO_PORT, KEY1_GPIO_PIN) == GPIO_PIN_RESET);
    uint8_t k2  = (HAL_GPIO_ReadPin(KEY2_GPIO_PORT, KEY2_GPIO_PIN) == GPIO_PIN_RESET);
    uint8_t kup = (HAL_GPIO_ReadPin(WKUP_GPIO_PORT, WKUP_GPIO_PIN) == GPIO_PIN_SET);

    uint8_t any = 0u;

    /* KEY0 leading edge: auto-zero */
    if (k0 && !S.k0_prev) {
        do_auto_zero();
        any = 1u;
    }
    S.k0_prev = k0;

    /* KEY2 leading edge: cycle filter */
    if (k2 && !S.k2_prev) {
        S.filter_mode = (uint8_t)((S.filter_mode + 1u) % 3u);
        S.iir_init = 0u;
        S.med_idx  = 0u;
        any = 1u;
    }
    S.k2_prev = k2;

    /* KEY_UP held: zoom out (larger V/div) */
    if (kup)  { vdiv_increase(); any = 1u; }
    /* KEY1  held: zoom in  (smaller V/div) */
    if (k1)   { vdiv_decrease(); any = 1u; }

    return any;
}

/* ===========================================================================
 * Public entry point
 * =========================================================================*/
void oscilloscope_run(void)
{
    /* Initialise ADC acquisition */
    adc_dc_init();

    /* Initialise state */
    S.vdiv_idx    = VDIV_DEF_IDX;
    S.v_per_div   = VDIV_TABLE[S.vdiv_idx];   /* 0.020 V = 20 mV/div */
    S.v_center    = S.v_per_div * (float)(GRID_ROWS / 2); /* 0.100 V center */
    S.fft_mode    = 0u;
    S.filter_mode = FILT_AVG;
    S.v_zero      = 0.0f;
    S.calibrated  = 0u;
    S.v_now       = 0.0f;
    S.over_range  = 0u;
    S.iir_acc     = 0.0f;
    S.iir_init    = 0u;
    S.med_idx     = 0u;
    S.prev_touch  = 0u;
    S.k0_prev     = 0u;
    S.k2_prev     = 0u;

    for (int i = 0; i < N_SAMPLES; i++) g_samp[i] = 0.0f;

    /* Full initial draw */
    lcd_clear(C_SCR);
    g_back_color = C_GRD_BG;
    full_redraw();
    draw_keyhint();

    /* Main loop */
    while (1) {
        uint32_t t0 = HAL_GetTick();

        /* Acquire new ADC batch result */
        gen_samples();

        /* Touch / key input */
        uint8_t td = handle_touch();
        if (td == 255u) {
            adc_dc_stop();
            return;
        }
        uint8_t kd = handle_keys();

        /* Redraw */
        draw_grid();
        draw_ylabels();
        if (S.fft_mode) draw_fft_spectrum(); else draw_waveform();
        draw_readout();
        if (td || kd) {
            draw_infobar();
            draw_topbar();
        }

        /* LED heartbeat */
        if ((HAL_GetTick() / 500u) & 1u) LED0(1); else LED0(0);

        /* Frame-rate cap */
        uint32_t dt = HAL_GetTick() - t0;
        if (dt < FRAME_MS) delay_ms((uint16_t)(FRAME_MS - dt));
    }
}
