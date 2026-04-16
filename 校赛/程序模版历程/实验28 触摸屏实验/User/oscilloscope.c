/**
 * @file  oscilloscope.c
 * @brief Software oscilloscope + FFT spectrum analyser.
 *        Portrait 240x320 LCD, ~25 fps redraw, FPU-accelerated signal math.
 *
 * Layout (portrait 240x320):
 *   y=  0.. 24  Top bar: [BACK][SIN][SQR][TRI][MIX][TIME/FFT]
 *   y= 25..224  10x10 oscilloscope grid  (200x200 px, CELL=20x20)
 *   y=225..247  Frequency slider
 *   y=248..270  Amplitude slider
 *   y=273..287  Info bar  (t/div, V/div, signal freq)
 *   y=290..319  Key-hint bar  (printed once at start)
 *
 * Key controls:
 *   KEY0  (PE4, active-low) : stretch time axis   (t/div x 1.05 per frame)
 *   KEY2  (PE2, active-low) : compress time axis  (t/div / 1.05 per frame)
 *   KEY_UP(PA0, active-high): stretch volt axis   (V/div / 1.05 per frame)
 *   KEY1  (PE3, active-low) : compress volt axis  (V/div x 1.05 per frame)
 *
 * Touch controls:
 *   [BACK]        : return to main menu
 *   [SIN/SQR/TRI/MIX]: switch waveform
 *   [TIME/FFT]    : toggle time-domain / FFT view
 *   Freq slider   : drag to set signal frequency  (1..100 Hz, log scale)
 *   Amp  slider   : drag to set signal amplitude  (0.10..2.00 V, linear)
 */

#include "oscilloscope.h"
#include "chinese_font.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/KEY/key.h"
#include "./BSP/TOUCH/touch.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* GBK byte strings for oscilloscope button labels */
/* "返回" fan-hui */
static const uint8_t OSC_LBL_BACK[] = {0xB7,0xB5,0xBB,0xD8,0x00};
/* "正弦" zheng-xian */
static const uint8_t OSC_LBL_SIN[]  = {0xD5,0xFD,0xCF,0xD2,0x00};
/* "方波" fang-bo */
static const uint8_t OSC_LBL_SQR[]  = {0xB7,0xBD,0xB2,0xA8,0x00};
/* "三角" san-jiao */
static const uint8_t OSC_LBL_TRI[]  = {0xC8,0xFD,0xBD,0xC7,0x00};
/* "混叠" hun-die */
static const uint8_t OSC_LBL_MIX[]  = {0xBB,0xEC,0xB5,0xFE,0x00};
/* "频域" pin-yu */
static const uint8_t OSC_LBL_FFT[]  = {0xC6,0xB5,0xD3,0xF2,0x00};
/* "时域" shi-yu */
static const uint8_t OSC_LBL_TIME[] = {0xCA,0xB1,0xD3,0xF2,0x00};
/* "频率:" pin-lv */
static const uint8_t OSC_LBL_FREQ[] = {0xC6,0xB5,0xC2,0xCA,0x3A,0x00};
/* "幅值:" fu-zhi */
static const uint8_t OSC_LBL_AMP[]  = {0xB7,0xF9,0xD6,0xB5,0x3A,0x00};
/* "时:" */
static const uint8_t OSC_LBL_T[]    = {0xCA,0xB1,0x3A,0x00};
/* "幅:" */
static const uint8_t OSC_LBL_A[]    = {0xB7,0xF9,0x3A,0x00};
/* "K0:时+ K2:时- KUP:幅+ K1:幅-" */
static const uint8_t OSC_LBL_HINT[] = {
    0x4B,0x30,0x3A,0xCA,0xB1,0x2B,0x20,
    0x4B,0x32,0x3A,0xCA,0xB1,0x2D,0x20,
    0x4B,0x55,0x50,0x3A,0xB7,0xF9,0x2B,0x20,
    0x4B,0x31,0x3A,0xB7,0xF9,0x2D,0x00
};

#ifndef M_PI
#define M_PI 3.14159265358979f
#endif

/* ===========================================================================
 * Colour palette  (RGB-565)
 * =========================================================================*/
#define C_SCR      0x0008   /* screen background - near-black navy          */
#define C_GRD_BG   0x0000   /* oscilloscope grid background - black         */
#define C_GRD_MJ   0x2104   /* major grid lines - very dark grey            */
#define C_AXIS     0x4A69   /* centre crosshair - medium grey               */
#define C_TICK     0x3186   /* minor tick marks on crosshair                */
#define C_WAVE     0xFFE0   /* waveform - yellow                            */
#define C_FFT_BAR  0x07E0   /* FFT bars - bright green                      */
#define C_FFT_PK   0xF800   /* FFT dominant-frequency marker - red          */
#define C_TXT      0xFFFF   /* primary text - white                         */
#define C_TXT_DIM  0x8410   /* secondary text - grey                        */
#define C_BTN_OFF  0x39E7   /* button idle background                       */
#define C_INFO_BG  0x10A2   /* info-bar background - dark grey              */
#define C_SLI_TRK  0x3186   /* slider track                                 */
#define C_SLI_FILL 0x03BF   /* slider filled portion - blue                 */
#define C_SLI_KNOB 0xFFFF   /* slider thumb knob - white                    */

/* Top-bar button active colours (one per button) */
static const uint16_t BTN_ACT[6] = {
    0xC340,   /* BACK  - warm brown                                         */
    0x035F,   /* SIN   - vivid blue                                         */
    0x07C0,   /* SQR   - vivid green                                        */
    0xFC40,   /* TRI   - vivid orange                                       */
    0x780F,   /* MIX   - vivid purple                                       */
    0xFFE0,   /* FFT   - yellow (same as waveform to hint "spectrum")       */
};

/* ===========================================================================
 * Grid / display geometry  (all in portrait 240x320)
 * =========================================================================*/
#define GRID_COLS  10          /* horizontal divisions                       */
#define GRID_ROWS  10          /* vertical   divisions                       */
#define CELL_W     20          /* pixels per horizontal division             */
#define CELL_H     20          /* pixels per vertical   division             */
#define OSC_LX     25          /* grid left  edge (Y labels in x= 0..24)    */
#define OSC_TY     25          /* grid top   edge (top bar  in y= 0..24)    */
#define OSC_W      (GRID_COLS * CELL_W)    /* 200                           */
#define OSC_H      (GRID_ROWS * CELL_H)    /* 200                           */
/* Grid pixels: x=[25..224], y=[25..224]                                     */
#define OSC_CX     (OSC_LX + OSC_W / 2)   /* centre x = 125                */
#define OSC_CY     (OSC_TY + OSC_H / 2)   /* centre y = 125                */

#define BAR_H      25          /* top button bar height                      */

/* Slider geometry */
#define SLI_LX     52          /* slider track left  (after label "Freq:")   */
#define SLI_RX     199         /* slider track right (before value text)      */
#define SLI_TW     (SLI_RX - SLI_LX + 1)   /* 148 px                       */
#define SLI_FY     236         /* freq slider centre Y                        */
#define SLI_AY     258         /* amp  slider centre Y                        */
#define SLI_TOL    12          /* touch tolerance (+/- px) around centre Y   */

/* Info / hint rows */
#define INFO_Y     273
#define HINT_Y     292

/* ===========================================================================
 * Signal / FFT parameters
 * =========================================================================*/
#define N_SAMPLES  200         /* one per pixel column; must be <= N_FFT     */
#define N_FFT      256         /* radix-2 FFT size; zero-pad N_SAMPLES->256  */

#define FREQ_MIN   1.0f        /* Hz                                         */
#define FREQ_MAX   100.0f
#define AMP_MIN    0.10f       /* V                                          */
#define AMP_MAX    2.00f
#define TDIV_MIN   0.001f      /* 1 ms / div                                 */
#define TDIV_MAX   1.0f        /* 1 s  / div                                 */
#define VDIV_MIN   0.02f       /* 20 mV / div                                */
#define VDIV_MAX   5.0f        /* 5 V  / div                                 */
#define ZOOM_K     1.05f       /* scale change per frame when key held       */
#define FRAME_MS   40          /* target frame period ms  (~25 fps)          */

/* ===========================================================================
 * State
 * =========================================================================*/
typedef enum { SIG_SIN = 0, SIG_SQR, SIG_TRI, SIG_MIX } SigType;

typedef struct {
    SigType sig;
    float   freq;        /* Hz  */
    float   amp;         /* V   */
    float   t_per_div;   /* s/div  */
    float   v_per_div;   /* V/div  */
    uint8_t fft_mode;    /* 0 = time domain, 1 = FFT  */
    float   sli_freq;    /* slider position [0..1]  */
    float   sli_amp;
} OscState;

static OscState  S;
static float     g_samp[N_SAMPLES];
static float     g_re[N_FFT];
static float     g_im[N_FFT];
static uint8_t   g_prev_touch;   /* for edge detection  */

/* ===========================================================================
 * Utility
 * =========================================================================*/
static float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/* ===========================================================================
 * Signal generation
 * =========================================================================*/
static float signal_at(float t)
{
    float ft = S.freq * t;
    float ph = ft - (float)(int32_t)ft;   /* fractional part [0, 1) */
    if (ph < 0.0f) ph += 1.0f;

    switch (S.sig) {
        case SIG_SIN:
            return S.amp * sinf(2.0f * M_PI * ft);
        case SIG_SQR:
            return S.amp * (ph < 0.5f ? 1.0f : -1.0f);
        case SIG_TRI:
            return S.amp * (ph < 0.5f ? (4.0f * ph - 1.0f)
                                       : (3.0f - 4.0f * ph));
        case SIG_MIX:
            /* Odd-harmonic mix: fundamental + 3rd + 5th (approximates square) */
            return S.amp * (0.637f * sinf(2.0f * M_PI * ft)
                          + 0.212f * sinf(6.0f * M_PI * ft)
                          + 0.127f * sinf(10.0f * M_PI * ft));
        default: return 0.0f;
    }
}

static void gen_samples(void)
{
    float t_total = (float)GRID_COLS * S.t_per_div;
    float dt      = t_total / (float)(N_SAMPLES - 1);
    for (int i = 0; i < N_SAMPLES; i++) {
        g_samp[i] = signal_at(i * dt);
    }
}

/* ===========================================================================
 * Radix-2 Cooley-Tukey FFT  (in-place, complex)
 * =========================================================================*/
static void do_fft(void)
{
    /* Fill re[] with Hanning-windowed samples, zero-pad rest; im[]=0 */
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

    /* Bit-reversal permutation */
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

    /* Butterfly stages */
    for (int len = 2; len <= N_FFT; len <<= 1) {
        float ang  = -2.0f * M_PI / (float)len;
        float wbRe = cosf(ang);
        float wbIm = sinf(ang);
        for (int i = 0; i < N_FFT; i += len) {
            float cRe = 1.0f, cIm = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                float uR = g_re[i + k];
                float uI = g_im[i + k];
                float vR = g_re[i + k + len/2] * cRe - g_im[i + k + len/2] * cIm;
                float vI = g_re[i + k + len/2] * cIm + g_im[i + k + len/2] * cRe;
                g_re[i + k]         = uR + vR;
                g_im[i + k]         = uI + vI;
                g_re[i + k + len/2] = uR - vR;
                g_im[i + k + len/2] = uI - vI;
                float nRe = cRe * wbRe - cIm * wbIm;
                cIm = cRe * wbIm + cIm * wbRe;
                cRe = nRe;
            }
        }
    }
}

/* ===========================================================================
 * Top button bar
 * =========================================================================*/
static const uint8_t * const BTN_LBL[6] = {
    OSC_LBL_BACK, OSC_LBL_SIN, OSC_LBL_SQR, OSC_LBL_TRI, OSC_LBL_MIX, OSC_LBL_FFT
};

static uint16_t str_w16(const uint8_t *s)
{
    uint16_t w = 0;
    while (*s)
    {
        if (*s >= 0x81 && *s <= 0xFE && *(s + 1) != 0) {
            w += 16;
            s += 2;
        } else {
            w += 8;
            s++;
        }
    }
    return w;
}

static void draw_topbar(void)
{
    uint16_t bw = (uint16_t)(lcddev.width / 6);   /* ~40 px each */
    for (int i = 0; i < 6; i++) {
        uint16_t x0 = (uint16_t)(i * bw);
        uint16_t x1 = (i == 5) ? (uint16_t)(lcddev.width - 1)
                                : (uint16_t)(x0 + bw - 1);
        /* Active state: highlight signal-type or FFT button */
        uint8_t active = 0;
        if (i == 1 && S.sig == SIG_SIN)  active = 1;
        if (i == 2 && S.sig == SIG_SQR)  active = 1;
        if (i == 3 && S.sig == SIG_TRI)  active = 1;
        if (i == 4 && S.sig == SIG_MIX)  active = 1;
        if (i == 5 && S.fft_mode)        active = 1;

        uint16_t bg = active ? BTN_ACT[i] : C_BTN_OFF;
        lcd_fill(x0, 0, x1, BAR_H - 1, bg);
        lcd_draw_rectangle(x0, 0, x1, BAR_H - 1, C_TXT_DIM);

        /* Button label - toggle FFT<->TIME on button 5 */
        const uint8_t *lbl = (i == 5 && !S.fft_mode) ? OSC_LBL_TIME : BTN_LBL[i];
        uint16_t lw  = str_w16(lbl);
        uint16_t tx  = x0 + (x1 - x0 + 1u - lw) / 2u;
        uint16_t ty  = (BAR_H - 16u) / 2u;
        lcd_show_chinese_str(tx, ty, lbl, active ? C_TXT : C_TXT_DIM);
    }
}

/* ===========================================================================
 * Oscilloscope grid  (black background + grey grid lines + crosshair ticks)
 * =========================================================================*/
static void draw_grid(void)
{
    /* Black fill */
    lcd_fill(OSC_LX, OSC_TY,
             OSC_LX + OSC_W - 1, OSC_TY + OSC_H - 1, C_GRD_BG);

    /* Horizontal grid lines */
    for (int r = 0; r <= GRID_ROWS; r++) {
        uint16_t y = (uint16_t)(OSC_TY + r * CELL_H);
        uint16_t c = (r == GRID_ROWS / 2) ? C_AXIS : C_GRD_MJ;
        lcd_draw_hline(OSC_LX, y, OSC_W, c);
    }
    /* Vertical grid lines */
    for (int col = 0; col <= GRID_COLS; col++) {
        uint16_t x = (uint16_t)(OSC_LX + col * CELL_W);
        uint16_t c = (col == GRID_COLS / 2) ? C_AXIS : C_GRD_MJ;
        lcd_draw_line(x, OSC_TY, x, OSC_TY + OSC_H, c);
    }
    /* Minor tick marks on centre crosshair (every CELL/5 pixels) */
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
 * Y-axis voltage labels  (left of grid, x=0..OSC_LX-1)
 * =========================================================================*/
static void fmt_volt_label(char *buf, int sz, float v)
{
    if (fabsf(v) < 0.0005f) {
        snprintf(buf, sz, " 0");
    } else if (fabsf(v) >= 10.0f) {
        snprintf(buf, sz, "%+.0fV", v);
    } else if (fabsf(v) >= 1.0f) {
        snprintf(buf, sz, "%+.1f", v);      /* e.g. "+1.5" */
    } else {
        snprintf(buf, sz, "%+.0fm", v * 1000.0f);  /* e.g. "+500" (mV) */
    }
}

static void draw_ylabels(void)
{
    char buf[8];
    lcd_fill(0, OSC_TY, OSC_LX - 1, OSC_TY + OSC_H - 1, C_SCR);
    /* Label rows 2, 4, 5(centre), 6, 8 */
    static const int ROWS[] = {2, 4, 5, 6, 8};
    for (int ri = 0; ri < 5; ri++) {
        int    r  = ROWS[ri];
        float  v  = ((float)(GRID_ROWS / 2) - (float)r) * S.v_per_div;
        int16_t ly = (int16_t)(OSC_TY + r * CELL_H) - 6;
        if (ly < (int16_t)OSC_TY)             ly = (int16_t)OSC_TY;
        if (ly + 12 > (int16_t)(OSC_TY + OSC_H)) continue;
        fmt_volt_label(buf, sizeof(buf), v);
        uint16_t tc = (r == GRID_ROWS / 2) ? C_AXIS : C_TXT_DIM;
        lcd_show_string(0, (uint16_t)ly, OSC_LX, 12, 12, buf, tc);
    }
}

/* ===========================================================================
 * Sliders
 * =========================================================================*/
static void draw_one_slider(uint16_t cy, float pos,
                             const uint8_t *lbl, const char *valstr)
{
    /* Clear row */
    lcd_fill(0, cy - 13, lcddev.width - 1, cy + 13, C_SCR);
    /* Label */
    lcd_show_chinese_str(0, cy - 8, lbl, C_TXT_DIM);
    /* Track */
    lcd_fill(SLI_LX, cy - 3, SLI_RX, cy + 3, C_SLI_TRK);
    /* Filled portion */
    uint16_t tx = (uint16_t)(SLI_LX + pos * (float)SLI_TW);
    if (tx > SLI_RX) tx = SLI_RX;
    if (tx > SLI_LX) lcd_fill(SLI_LX, cy - 3, tx, cy + 3, C_SLI_FILL);
    /* Knob (11 x 11 white square) */
    lcd_fill(tx - 5, cy - 6, tx + 5, cy + 6, C_SLI_KNOB);
    /* Value text */
    lcd_show_string(SLI_RX + 4, cy - 6, lcddev.width - SLI_RX - 4,
                    12, 12, (char *)valstr, C_TXT);
}

static void draw_sliders(void)
{
    char buf[12];
    /* Frequency */
    float f = S.freq;
    if (f < 10.0f)        snprintf(buf, sizeof(buf), "%.1fHz",  f);
    else if (f < 1000.0f) snprintf(buf, sizeof(buf), "%.0fHz",  f);
    else                   snprintf(buf, sizeof(buf), "%.1fkHz", f / 1000.0f);
    draw_one_slider(SLI_FY, S.sli_freq, OSC_LBL_FREQ, buf);
    /* Amplitude */
    snprintf(buf, sizeof(buf), "%.2fV", S.amp);
    draw_one_slider(SLI_AY, S.sli_amp, OSC_LBL_AMP, buf);
}

/* ===========================================================================
 * Info bar  (t/div, V/div, waveform freq displayed once per redraw)
 * =========================================================================*/
static void fmt_time_str(char *buf, int sz, float t)
{
    if (t < 0.001f)      snprintf(buf, sz, "%.0fus", t * 1.0e6f);
    else if (t < 1.0f)   snprintf(buf, sz, "%.1fms", t * 1000.0f);
    else                  snprintf(buf, sz, "%.2fs",  t);
}

static void fmt_volt_str(char *buf, int sz, float v)
{
    if (v < 0.1f)       snprintf(buf, sz, "%.0fmV", v * 1000.0f);
    else if (v < 10.0f) snprintf(buf, sz, "%.2fV",  v);
    else                 snprintf(buf, sz, "%.1fV",  v);
}

static void draw_infobar(void)
{
    char t_s[10], v_s[10];
    lcd_fill(0, INFO_Y - 1, lcddev.width - 1, INFO_Y + 13, C_INFO_BG);
    fmt_time_str(t_s, sizeof(t_s), S.t_per_div);
    fmt_volt_str(v_s, sizeof(v_s), S.v_per_div);
    lcd_show_chinese_str(0, INFO_Y, OSC_LBL_T, C_TXT_DIM);
    lcd_show_string(24, INFO_Y, 90, 12, 12, t_s, C_TXT_DIM);
    lcd_show_chinese_str(120, INFO_Y, OSC_LBL_A, C_TXT_DIM);
    lcd_show_string(144, INFO_Y, 90, 12, 12, v_s, C_TXT_DIM);
}

/* ===========================================================================
 * Key-hint bar  (drawn once at startup, no erase needed)
 * =========================================================================*/
static void draw_keyhint(void)
{
    lcd_fill(0, HINT_Y, lcddev.width - 1, lcddev.height - 1, C_SCR);
    lcd_show_chinese_str(0, HINT_Y, OSC_LBL_HINT, C_TXT_DIM);
}

/* ===========================================================================
 * Waveform drawing  (time domain)
 * =========================================================================*/
static void draw_waveform(void)
{
    float pxv = (float)CELL_H / S.v_per_div;   /* pixels per volt */
    int16_t prev_px = -1, prev_py = -1;

    for (int i = 0; i < N_SAMPLES; i++) {
        int16_t px = (int16_t)(OSC_LX + (int32_t)i * (OSC_W - 1) / (N_SAMPLES - 1));
        int16_t py = (int16_t)(OSC_CY - g_samp[i] * pxv + 0.5f);
        if (py < (int16_t)OSC_TY)           py = (int16_t)OSC_TY;
        if (py > (int16_t)(OSC_TY + OSC_H - 1)) py = (int16_t)(OSC_TY + OSC_H - 1);

        if (prev_px >= 0) {
            lcd_draw_line((uint16_t)prev_px, (uint16_t)prev_py,
                          (uint16_t)px,      (uint16_t)py, C_WAVE);
        }
        prev_px = px;
        prev_py = py;
    }
}

/* ===========================================================================
 * FFT spectrum drawing
 * =========================================================================*/
static void draw_fft_spectrum(void)
{
    do_fft();

    int   nbins = N_FFT / 2;
    float peak_mag = 0.0f;
    int   peak_bin = 1;

    /* Find dominant bin (skip DC = bin 0) */
    for (int k = 1; k < nbins; k++) {
        float m = sqrtf(g_re[k] * g_re[k] + g_im[k] * g_im[k]);
        if (m > peak_mag) { peak_mag = m; peak_bin = k; }
    }
    if (peak_mag < 1e-6f) peak_mag = 1e-6f;   /* avoid /0 */

    /* Draw bar for each pixel column */
    for (int x = 0; x < OSC_W; x++) {
        int   k   = (int)((float)x * (float)nbins / (float)OSC_W);
        if (k >= nbins) k = nbins - 1;
        float mag = sqrtf(g_re[k] * g_re[k] + g_im[k] * g_im[k]) / peak_mag;
        int16_t bh = (int16_t)(mag * (float)(OSC_H - 10) + 0.5f);
        if (bh < 0) bh = 0;
        if (bh > OSC_H - 10) bh = OSC_H - 10;

        uint16_t sx = (uint16_t)(OSC_LX + x);
        uint16_t y1 = (uint16_t)(OSC_TY + OSC_H - 1 - bh);
        uint16_t y2 = (uint16_t)(OSC_TY + OSC_H - 1);
        uint16_t fc = (k == peak_bin) ? C_FFT_PK : C_FFT_BAR;
        if (bh > 0) lcd_draw_line(sx, y1, sx, y2, fc);
    }

    /* Label: peak frequency */
    float sr    = (float)N_SAMPLES / ((float)GRID_COLS * S.t_per_div);
    float pfreq = (float)peak_bin * sr / (float)N_FFT;
    char  buf[20];
    snprintf(buf, sizeof(buf), "%.1fHz", pfreq);
    /* Draw label box then text */
    lcd_fill(OSC_LX + 2, OSC_TY + 2, OSC_LX + 150, OSC_TY + 18, C_GRD_BG);
    lcd_show_chinese_str(OSC_LX + 4, OSC_TY + 2, OSC_LBL_FREQ, C_FFT_PK);
    lcd_show_string(OSC_LX + 52, OSC_TY + 5, 96, 12, 12, buf, C_FFT_PK);

    /* X-axis frequency labels (bottom of grid area) */
    float nyq = sr / 2.0f;
    char lbuf[10];
    fmt_time_str(lbuf, sizeof(lbuf), nyq < 1.0f ? nyq : 0.0f);
    /* Label at 0, 1/4, 1/2 of Nyquist */
    lcd_show_string(OSC_LX,           OSC_TY + OSC_H - 13, 30, 12, 12, "0",   C_TXT_DIM);
    snprintf(buf, sizeof(buf), "%.0fHz", nyq / 2.0f);
    lcd_show_string(OSC_CX - 12,      OSC_TY + OSC_H - 13, 40, 12, 12, buf,   C_TXT_DIM);
    snprintf(buf, sizeof(buf), "%.0fHz", nyq);
    lcd_show_string(OSC_LX + OSC_W - 36, OSC_TY + OSC_H - 13, 40, 12, 12, buf, C_TXT_DIM);
}

/* ===========================================================================
 * Full screen redraw
 * =========================================================================*/
static void full_redraw(void)
{
    draw_topbar();
    draw_grid();
    draw_ylabels();
    gen_samples();
    if (S.fft_mode) {
        draw_fft_spectrum();
    } else {
        draw_waveform();
    }
    draw_sliders();
    draw_infobar();
}

/* ===========================================================================
 * Touch input handler
 * Returns 0  = no change
 *         1  = redraw needed
 *         255 = BACK pressed (exit)
 * =========================================================================*/
static uint8_t handle_touch(void)
{
    tp_dev.scan(0);
    uint8_t now   = (tp_dev.sta & TP_PRES_DOWN) ? 1u : 0u;
    uint8_t dn    = now && !g_prev_touch;
    g_prev_touch  = now;
    if (!now) return 0;

    uint16_t tx = tp_dev.x[0];
    uint16_t ty = tp_dev.y[0];

    /* --- Top bar buttons (single press) --------------------------------- */
    if (ty < BAR_H && dn) {
        uint16_t bw  = (uint16_t)(lcddev.width / 6);
        int      btn = (int)(tx / bw);
        if (btn > 5) btn = 5;
        switch (btn) {
            case 0: return 255u;               /* BACK */
            case 1: S.sig = SIG_SIN; break;
            case 2: S.sig = SIG_SQR; break;
            case 3: S.sig = SIG_TRI; break;
            case 4: S.sig = SIG_MIX; break;
            case 5: S.fft_mode ^= 1u;  break;
        }
        return 1u;
    }

    /* --- Frequency slider (continuous drag) ----------------------------- */
    if (ty >= SLI_FY - SLI_TOL && ty <= SLI_FY + SLI_TOL
            && tx >= SLI_LX && tx <= SLI_RX) {
        float pos = (float)(tx - SLI_LX) / (float)SLI_TW;
        pos = clampf(pos, 0.0f, 1.0f);
        if (pos != S.sli_freq) {
            S.sli_freq = pos;
            S.freq = FREQ_MIN * powf(FREQ_MAX / FREQ_MIN, pos);
            return 1u;
        }
    }

    /* --- Amplitude slider (continuous drag) ----------------------------- */
    if (ty >= SLI_AY - SLI_TOL && ty <= SLI_AY + SLI_TOL
            && tx >= SLI_LX && tx <= SLI_RX) {
        float pos = (float)(tx - SLI_LX) / (float)SLI_TW;
        pos = clampf(pos, 0.0f, 1.0f);
        if (pos != S.sli_amp) {
            S.sli_amp = pos;
            S.amp = AMP_MIN + pos * (AMP_MAX - AMP_MIN);
            return 1u;
        }
    }

    return 0u;
}

/* ===========================================================================
 * Key input handler  (direct GPIO read for smooth held-key zoom)
 * Returns 1 if a zoom key is active (redraw needed), else 0.
 * =========================================================================*/
static uint8_t handle_keys(void)
{
    uint8_t k0  = (HAL_GPIO_ReadPin(KEY0_GPIO_PORT, KEY0_GPIO_PIN) == GPIO_PIN_RESET);
    uint8_t k1  = (HAL_GPIO_ReadPin(KEY1_GPIO_PORT, KEY1_GPIO_PIN) == GPIO_PIN_RESET);
    uint8_t k2  = (HAL_GPIO_ReadPin(KEY2_GPIO_PORT, KEY2_GPIO_PIN) == GPIO_PIN_RESET);
    uint8_t kup = (HAL_GPIO_ReadPin(WKUP_GPIO_PORT, WKUP_GPIO_PIN) == GPIO_PIN_SET);

    uint8_t any = 0;
    if (k0)  { S.t_per_div = clampf(S.t_per_div * ZOOM_K, TDIV_MIN, TDIV_MAX); any = 1; }
    if (k2)  { S.t_per_div = clampf(S.t_per_div / ZOOM_K, TDIV_MIN, TDIV_MAX); any = 1; }
    if (kup) { S.v_per_div = clampf(S.v_per_div / ZOOM_K, VDIV_MIN, VDIV_MAX); any = 1; }
    if (k1)  { S.v_per_div = clampf(S.v_per_div * ZOOM_K, VDIV_MIN, VDIV_MAX); any = 1; }
    return any;
}

/* ===========================================================================
 * Public entry point
 * =========================================================================*/
void oscilloscope_run(void)
{
    /* Initialise state with sensible defaults */
    S.sig       = SIG_SIN;
    S.freq      = 10.0f;
    S.amp       = 1.0f;
    S.t_per_div = 0.02f;    /* 20 ms/div -> 200 ms total -> 2 cycles of 10 Hz */
    S.v_per_div = 0.50f;    /* 0.5 V/div -> +-2.5 V range                     */
    S.fft_mode  = 0u;
    /* Compute initial slider positions */
    S.sli_freq  = logf(S.freq / FREQ_MIN) / logf(FREQ_MAX / FREQ_MIN);
    S.sli_amp   = (S.amp - AMP_MIN) / (AMP_MAX - AMP_MIN);
    g_prev_touch = 0u;

    /* Full initial draw */
    lcd_clear(C_SCR);
    g_back_color = C_GRD_BG;   /* fix: set char background to black (default is white) */
    full_redraw();
    draw_keyhint();

    /* Main loop */
    while (1) {
        uint32_t t0 = HAL_GetTick();

        uint8_t  td = handle_touch();
        if (td == 255u) return;          /* [BACK] touched -> exit */

        uint8_t kd = handle_keys();

        if (td || kd) {
            /* Partial redraw: grid + labels + waveform/FFT + sliders + info */
            draw_grid();
            draw_ylabels();
            gen_samples();
            if (S.fft_mode) draw_fft_spectrum(); else draw_waveform();
            draw_sliders();
            draw_infobar();
            draw_topbar();          /* refresh active-button highlight        */
        }

        /* LED heartbeat */
        if ((HAL_GetTick() / 500u) & 1u) LED0(1); else LED0(0);

        /* Frame-rate cap */
        uint32_t dt = HAL_GetTick() - t0;
        if (dt < FRAME_MS) delay_ms(FRAME_MS - (uint16_t)dt);
    }
}
