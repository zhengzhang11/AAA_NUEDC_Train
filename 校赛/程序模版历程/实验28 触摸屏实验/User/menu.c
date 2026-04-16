/**
 * @file  menu.c
 * @brief Touch-screen main menu for ATK-Explorer STM32F407 V3.
 *        Portrait 240x320.  Three apps: Dino Game / Oscilloscope / Motor Monitor.
 *        All comments are pure ASCII (Keil GBK-safe).
 */

#include "menu.h"
#include "dino_game.h"
#include "oscilloscope.h"
#include "motor_monitor.h"
#include "chinese_font.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/TOUCH/touch.h"
#include "./SYSTEM/delay/delay.h"
#include <string.h>

/* -----------------------------------------------------------------------
 * Color palette
 * --------------------------------------------------------------------- */
#define C_BG        BLACK
#define C_TITLE     YELLOW
#define C_BTN_DINO  0x07E0u    /* green   */
#define C_BTN_OSC   0x001Fu    /* blue    */
#define C_BTN_MON   0xF81Fu    /* magenta */
#define C_BTN_TEXT  WHITE
#define C_BORDER    0xFFFFu    /* white border */
#define C_PRESS     0x8410u    /* gray flash on press */
#define C_SUBTITLE  0x7BEFu    /* light gray */

/* -----------------------------------------------------------------------
 * Layout  (portrait 240x320)
 * Title   y=35
 * Sub     y=65
 * Btn 1   y=90..149   (60 px tall)
 * Btn 2   y=160..219
 * Btn 3   y=230..289
 * Hint    y=305
 * --------------------------------------------------------------------- */
#define TITLE_Y     35
#define SUB_Y       65

#define BTN_X       20
#define BTN_W      200
#define BTN_H       60

#define BTN1_Y      90
#define BTN2_Y     160
#define BTN3_Y     230

#define HINT_Y     305

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */
static void draw_button(uint16_t x, uint16_t y,
                        uint16_t w, uint16_t h,
                        uint16_t fill, const char *label)
{
    lcd_draw_rectangle(x - 1u, y - 1u, x + w, y + h, C_BORDER);
    lcd_fill(x, y, x + w - 1u, y + h - 1u, fill);

    uint16_t tlen = (uint16_t)(strlen(label) * 8u);
    uint16_t tx   = (tlen < w) ? (uint16_t)(x + (w - tlen) / 2u) : x;
    uint16_t ty   = (uint16_t)(y + (h - 16u) / 2u);
    lcd_show_string(tx, ty, w, 16u, 16u, (char *)label, C_BTN_TEXT);
}

static void flash_button(uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h, const uint8_t *label)
{
    uint16_t tlen = 0;
    const uint8_t *p = label;

    while (*p)
    {
        if (*p >= 0x81 && *p <= 0xFE && *(p + 1) != 0) {
            tlen += 16;
            p += 2;
        } else {
            tlen += 8;
            p += 1;
        }
    }

    lcd_fill(x, y, x + w - 1u, y + h - 1u, C_PRESS);
    uint16_t tx   = (tlen < w) ? (uint16_t)(x + (w - tlen) / 2u) : x;
    uint16_t ty   = (uint16_t)(y + (h - 16u) / 2u);
    lcd_show_chinese_str(tx, ty, label, C_BTN_TEXT);
    delay_ms(120u);
}

static uint8_t hit_test(uint16_t px, uint16_t py,
                        uint16_t x,  uint16_t y,
                        uint16_t w,  uint16_t h)
{
    return (px >= x && px < (uint16_t)(x + w) &&
            py >= y && py < (uint16_t)(y + h)) ? 1u : 0u;
}

/* -----------------------------------------------------------------------
 * draw_menu() - full repaint
 * --------------------------------------------------------------------- */
/* GBK byte strings for menu labels */
/* "选择功能:"  xuan-ze-gong-neng-: */
static const uint8_t STR_SELECT[]   = {0xD1,0xA1,0xD4,0xF1,0xB9,0xA6,0xC4,0xDC,0x3A,0x00};
/* "恐龙游戏"  kong-long-you-xi */
static const uint8_t STR_DINO[]     = {0xBF,0xD6,0xC1,0xFA,0xD3,0xCE,0xCF,0xB7,0x00};
/* "示波器"    shi-bo-qi */
static const uint8_t STR_OSC[]      = {0xCA,0xBE,0xB2,0xA8,0xC6,0xF7,0x00};
/* "电机监控"  dian-ji-jian-kong */
static const uint8_t STR_MON[]      = {0xB5,0xE7,0xBB,0xFA,0xBC,0xE0,0xBF,0xD8,0x00};
/* "触摸按钮进入"  chu-mo-an-niu-jin-ru */
static const uint8_t STR_HINT[]     = {0xB4,0xA5,0xC3,0xFE,0xB0,0xB4,0xE2,0x6F,0xBD,0xF8,0xC8,0xEB,0x00};

static void draw_menu(void)
{
    g_back_color = 0x0000u;   /* fix: text background = black (default is white) */
    lcd_clear(C_BG);

    /* Title: "ATK-F407 LAB" (ASCII, 12 chars * 12px ~ 144px centered) */
    lcd_show_string(32u, TITLE_Y, 180u, 24u, 24u, "ATK-F407 LAB", C_TITLE);

    /* Subtitle: "选择功能:" (5 chars * 16px = 80px) */
    lcd_show_chinese_str(28u, SUB_Y, STR_SELECT, C_SUBTITLE);

    /* Buttons with Chinese labels drawn manually (Chinese chars don't fit draw_button) */
    /* BTN1: green - 恐龙游戏 (4 chars * 16px = 64px) */
    lcd_draw_rectangle((uint16_t)(BTN_X - 1u), BTN1_Y - 1u,
                       (uint16_t)(BTN_X + BTN_W), (uint16_t)(BTN1_Y + BTN_H), C_BORDER);
    lcd_fill(BTN_X, BTN1_Y, (uint16_t)(BTN_X + BTN_W - 1u),
             (uint16_t)(BTN1_Y + BTN_H - 1u), C_BTN_DINO);
    lcd_show_chinese_str((uint16_t)(BTN_X + (BTN_W - 64u) / 2u),
                         (uint16_t)(BTN1_Y + (BTN_H - 16u) / 2u), STR_DINO, C_BTN_TEXT);

    /* BTN2: blue - 示波器 (3 chars * 16px = 48px) */
    lcd_draw_rectangle((uint16_t)(BTN_X - 1u), BTN2_Y - 1u,
                       (uint16_t)(BTN_X + BTN_W), (uint16_t)(BTN2_Y + BTN_H), C_BORDER);
    lcd_fill(BTN_X, BTN2_Y, (uint16_t)(BTN_X + BTN_W - 1u),
             (uint16_t)(BTN2_Y + BTN_H - 1u), C_BTN_OSC);
    lcd_show_chinese_str((uint16_t)(BTN_X + (BTN_W - 48u) / 2u),
                         (uint16_t)(BTN2_Y + (BTN_H - 16u) / 2u), STR_OSC, C_BTN_TEXT);

    /* BTN3: magenta - 电机监控 (4 chars * 16px = 64px) */
    lcd_draw_rectangle((uint16_t)(BTN_X - 1u), BTN3_Y - 1u,
                       (uint16_t)(BTN_X + BTN_W), (uint16_t)(BTN3_Y + BTN_H), C_BORDER);
    lcd_fill(BTN_X, BTN3_Y, (uint16_t)(BTN_X + BTN_W - 1u),
             (uint16_t)(BTN3_Y + BTN_H - 1u), C_BTN_MON);
    lcd_show_chinese_str((uint16_t)(BTN_X + (BTN_W - 64u) / 2u),
                         (uint16_t)(BTN3_Y + (BTN_H - 16u) / 2u), STR_MON, C_BTN_TEXT);

    /* Hint: "触摸按钮进入" (6 chars * 16px = 96px) */
    lcd_show_chinese_str((uint16_t)((240u - 96u) / 2u), HINT_Y, STR_HINT, C_SUBTITLE);
}

/* -----------------------------------------------------------------------
 * main_menu_run() - entry point, never returns
 * --------------------------------------------------------------------- */
void main_menu_run(void)
{
    uint8_t prev_down = 0u;

    draw_menu();

    while (1)
    {
        tp_dev.scan(0);
        uint8_t down = (tp_dev.sta & TP_PRES_DOWN) ? 1u : 0u;

        if (down && !prev_down)   /* leading-edge touch only */
        {
            uint16_t tx = tp_dev.x[0];
            uint16_t ty = tp_dev.y[0];

            if (hit_test(tx, ty, BTN_X, BTN1_Y, BTN_W, BTN_H))
            {
                flash_button(BTN_X, BTN1_Y, BTN_W, BTN_H, STR_DINO);
                dino_game_run();    /* returns on KEY2 */
                draw_menu();
            }
            else if (hit_test(tx, ty, BTN_X, BTN2_Y, BTN_W, BTN_H))
            {
                flash_button(BTN_X, BTN2_Y, BTN_W, BTN_H, STR_OSC);
                oscilloscope_run(); /* returns on [BACK] touch */
                draw_menu();
            }
            else if (hit_test(tx, ty, BTN_X, BTN3_Y, BTN_W, BTN_H))
            {
                flash_button(BTN_X, BTN3_Y, BTN_W, BTN_H, STR_MON);
                motor_monitor_run(); /* returns on [BACK] touch */
                draw_menu();
            }
        }

        prev_down = down;
        delay_ms(20u);
    }
}
