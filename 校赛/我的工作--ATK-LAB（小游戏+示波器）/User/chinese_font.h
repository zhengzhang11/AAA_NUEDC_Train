/**
 * @file  chinese_font.h
 * @brief Embedded 16x16 Chinese character bitmaps (SimHei, GBK encoding).
 *        63 characters, sorted by GBK code for binary search.
 */
#ifndef __CHINESE_FONT_H
#define __CHINESE_FONT_H

#include "./SYSTEM/sys/sys.h"

/** One record: GBK code (2 bytes) + 32 bitmap bytes (16 rows * 2 bytes/row) */
typedef struct {
    uint16_t gbk;
    uint8_t  bmp[32];
} ChineseChar16_t;

/** Total number of embedded Chinese characters */
#define CHINESE_CHAR_COUNT  80

/** The character table stored in Flash */
extern const ChineseChar16_t g_chinese_chars[CHINESE_CHAR_COUNT];

/**
 * @brief Draw one 16x16 Chinese character at (x,y).
 * @param x,y    Top-left pixel position
 * @param gbk    16-bit GBK code (high byte first)
 * @param color  Foreground color (RGB565)
 */
void lcd_show_chinese_char(uint16_t x, uint16_t y, uint16_t gbk, uint16_t color);

/**
 * @brief Draw a mixed ASCII/Chinese string.
 *        ASCII chars use lcd_show_char (8x16), Chinese chars use lcd_show_chinese_char (16x16).
 * @param x,y    Top-left position
 * @param str    Pointer to GBK-encoded byte string (NULL-terminated)
 * @param color  Foreground color
 */
void lcd_show_chinese_str(uint16_t x, uint16_t y, const uint8_t *str, uint16_t color);

#endif /* __CHINESE_FONT_H */
