/**
 ****************************************************************************************************
 * @file        dino_game.c
 * @brief       Chrome 风格恐龙跳跃游戏 - 正点原子探索者 STM32F407 V3
 *
 * 帧率     : ~25 fps  (FRAME_MS = 40 ms/帧)
 * 渲染方式 : 用背景色擦除旧位置，再在新位置绘制
 *
 * 按键说明:
 *   KEY_UP (WK_UP / PA0) : 跳跃 / 开始游戏 / 重新开始
 *   KEY0   (PE4)          : 俯冲（长按，直接读 GPIO，低电平有效）
 *   KEY1   (PE3)          : 死亡后重新开始
 ****************************************************************************************************
 */

#include "dino_game.h"
#include "chinese_font.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/KEY/key.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"

/* GBK byte strings for dino game UI */
/* "恐龙游戏" */
static const uint8_t DINO_STR_TITLE[] = {0xBF,0xD6,0xC1,0xFA,0xD3,0xCE,0xCF,0xB7,0x00};
/* "WK_UP:跳" */
static const uint8_t DINO_STR_JUMP[]  = {0x57,0x4B,0x5F,0x55,0x50,0x3A,0xCC,0xF8,0x00};
/* "KEY0:俯冲(长按)" */
static const uint8_t DINO_STR_DUCK[]  = {0x4B,0x45,0x59,0x30,0x3A,0xB8,0xA9,0xB3,0xE5,0x28,0xB3,0xA4,0xB0,0xB4,0x29,0x00};
/* "KEY1:再次游戏" */
static const uint8_t DINO_STR_REST[]  = {0x4B,0x45,0x59,0x31,0x3A,0xD4,0xD9,0xB4,0xCE,0xD3,0xCE,0xCF,0xB7,0x00};
/* "WK_UP开始游戏!" */
static const uint8_t DINO_STR_START[] = {0x57,0x4B,0x5F,0x55,0x50,0xBF,0xAA,0xCA,0xBC,0xD3,0xCE,0xCF,0xB7,0x21,0x00};
/* "分:" */
static const uint8_t DINO_STR_SC[]    = {0xB7,0xD6,0x3A,0x00};
/* "高:" */
static const uint8_t DINO_STR_HI[]    = {0xB8,0xDF,0x3A,0x00};
/* "游戏结束!" */
static const uint8_t DINO_STR_OVER[]  = {0xD3,0xCE,0xCF,0xB7,0xBD,0xE1,0xCA,0xF8,0x21,0x00};
/* "WK_UP/KEY1:再次游戏" */
static const uint8_t DINO_STR_RETRY[] = {0x57,0x4B,0x5F,0x55,0x50,0x2F,0x4B,0x45,0x59,0x31,0x3A,0xD4,0xD9,0xB4,0xCE,0xD3,0xCE,0xCF,0xB7,0x00};

/* ===========================================================================
 * 颜色定义
 * =========================================================================*/
#define BG_CLR      WHITE        /* 0xFFFF - 背景色                          */
#define DINO_CLR    BLACK        /* 0x0000 - 恐龙轮廓色                      */
#define CACT_CLR    0x0360       /* 深绿色  - 仙人掌色                       */
#define GND_CLR     GRAY         /* 0x8430 - 地面线颜色                      */
#define TXT_CLR     BLACK
#define DEAD_CLR    RED

/* ===========================================================================
 * 恐龙精灵尺寸
 *   站立 : 20 x 32   (DINO_W_S x DINO_H_S)
 *   俯冲 : 30 x 16   (DINO_W_D x DINO_H_D)
 * =========================================================================*/
#define DINO_X      50           /* 恐龙在屏幕上的固定水平位置               */
#define DINO_W_S    20
#define DINO_H_S    32
#define DINO_W_D    30
#define DINO_H_D    16

/* ===========================================================================
 * 物理参数（定点数 *8，亚像素精度）
 * =========================================================================*/
#define JUMP_VY_FP8   (-10 * 8) /* 跳跃初速度（向上）                       */
#define GRAVITY_FP8   (  1 * 8) /* 每帧重力加速度（向下）                   */

/* ===========================================================================
 * 游戏参数
 * =========================================================================*/
#define FRAME_MS          40    /* 目标帧周期 (ms)，约 25 fps               */
#define SPEED_INIT         3    /* 游戏开始时每帧移动像素数                  */
#define SPEED_MAX         10    /* 最大滚动速度（像素/帧）                   */
#define SCORE_SPEED_UP   150    /* 每增加多少分提升一次速度                  */
#define SPAWN_MIN_PX     110    /* 相邻仙人掌生成的最小像素间距              */
#define SPAWN_MAX_PX     260    /* 相邻仙人掌生成的最大像素间距              */
#define MAX_CACTI          3    /* 同时存在的仙人掌槽位数量                  */

/* ===========================================================================
 * 仙人掌类型表  { 宽度, 高度 }
 *   0 - 矮单株  (12 x 24)
 *   1 - 高单株  (12 x 32)
 *   2 - 双株簇  (26 x 32)
 * =========================================================================*/
typedef struct { uint8_t w; uint8_t h; } CactusDim;
static const CactusDim CD[3] = { {12,24}, {12,32}, {26,32} };

/* ===========================================================================
 * 游戏状态类型定义
 * =========================================================================*/
typedef enum { GAME_START, GAME_RUN, GAME_DEAD } GameState;

typedef struct {
    int16_t  y,   prev_y;    /* 恐龙精灵的顶部 Y 坐标                       */
    int16_t  vy_fp8;         /* 垂直速度（定点数 *8）                        */
    uint8_t  jumping;
    uint8_t  ducking,  prev_duck;
    uint8_t  anim,     anim_cnt; /* 0/1 跑步腿部交替动画帧                  */
} DinoState;

typedef struct {
    int16_t  x,   prev_x;   /* 仙人掌精灵的左边缘 X 坐标                   */
    uint8_t  type;           /* 0/1/2，索引 CD[] 表                         */
    uint8_t  active;
} CactusState;

/* ===========================================================================
 * 全局游戏上下文（static：存放在 BSS 段，不占用栈空间）
 * =========================================================================*/
static struct {
    GameState   state;
    DinoState   dino;
    CactusState cacti[MAX_CACTI];
    uint32_t    score, hi_score, frame;
    uint16_t    speed;
    uint16_t    ground_y;    /* 地面线的 Y 坐标                             */
    uint16_t    spawn_cnt;   /* 下次生成仙人掌的倒计时帧数                  */
} G;

/* ===========================================================================
 * 轻量级 xorshift32 伪随机数生成器
 * =========================================================================*/
static uint32_t lfsr = 0xDEADBEEFul;
static uint16_t rng(uint16_t max)
{
    lfsr ^= lfsr << 13;
    lfsr ^= lfsr >> 17;
    lfsr ^= lfsr << 5;
    return (uint16_t)(lfsr % max);
}

/* ===========================================================================
 * 底层绘图辅助函数
 * =========================================================================*/

/* 用背景色填充矩形区域，自动裁剪到屏幕边界 */
static void erase(int16_t x, int16_t y, uint16_t w, uint16_t h)
{
    int16_t x2 = x + (int16_t)w - 1;
    int16_t y2 = y + (int16_t)h - 1;
    if (x  < 0)                       x  = 0;
    if (y  < 0)                       y  = 0;
    if (x2 >= (int16_t)lcddev.width)  x2 = (int16_t)lcddev.width  - 1;
    if (y2 >= (int16_t)lcddev.height) y2 = (int16_t)lcddev.height - 1;
    if (x2 < x || y2 < y) return;    /* 完全在屏幕外，直接返回              */
    lcd_fill((uint16_t)x, (uint16_t)y, (uint16_t)x2, (uint16_t)y2, BG_CLR);
}

/* ===========================================================================
 * 恐龙精灵绘制
 *
 *  站立 (20 x 32):                 俯冲 (30 x 16):
 *          ########                          ########
 *  ##################   (身体)     ############################
 *  ########             (尾巴)     ##########
 *     ## ##             (腿部)
 * =========================================================================*/
static void draw_dino(int16_t x, int16_t y, uint8_t anim, uint8_t duck, uint16_t c)
{
    if (!duck) {
        /* --- 站立状态 (20 x 32) --- */
        lcd_fill(x+ 8, y,    x+19, y+ 9, c); /* 头部                       */
        lcd_fill(x+ 2, y+ 6, x+19, y+21, c); /* 身体                       */
        lcd_fill(x,    y+ 8, x+ 5, y+15, c); /* 尾巴                       */
        if (c != BG_CLR)
            lcd_draw_point(x+16, y+3, BG_CLR); /* 眼睛（头部镂空）          */
        /* 腿部：anim=0 右腿前伸，anim=1 左腿前伸 */
        if (anim == 0) {
            lcd_fill(x+ 4, y+20, x+ 8, y+31, c); /* 左腿（后摆）           */
            lcd_fill(x+12, y+18, x+16, y+31, c); /* 右腿（前伸）           */
        } else {
            lcd_fill(x+ 4, y+18, x+ 8, y+31, c); /* 左腿（前伸）           */
            lcd_fill(x+12, y+20, x+16, y+31, c); /* 右腿（后摆）           */
        }
    } else {
        /* --- 俯冲状态 (30 x 16) --- */
        lcd_fill(x+18, y,    x+29, y+ 9, c); /* 头部                       */
        lcd_fill(x+ 2, y+ 4, x+28, y+15, c); /* 身体                       */
        lcd_fill(x,    y+ 5, x+ 6, y+12, c); /* 尾巴                       */
        if (c != BG_CLR)
            lcd_draw_point(x+26, y+3, BG_CLR); /* 眼睛                      */
        lcd_fill(x+ 6, y+11, x+10, y+15, c); /* 左腿                       */
        lcd_fill(x+18, y+11, x+22, y+15, c); /* 右腿                       */
    }
}

/* ===========================================================================
 * 仙人掌精灵绘制
 * =========================================================================*/
static void draw_cact_small(int16_t x, int16_t y, uint16_t c)
{
    /* 矮单株 (12 x 24) */
    lcd_fill(x+4, y,    x+7,  y+23, c); /* 主干                             */
    lcd_fill(x,   y+2,  x+3,  y+14, c); /* 左侧臂 + 顶帽                   */
    lcd_fill(x+8, y,    x+11, y+12, c); /* 右侧臂 + 顶帽                   */
}

static void draw_cact_tall(int16_t x, int16_t y, uint16_t c)
{
    /* 高单株 (12 x 32) */
    lcd_fill(x+4, y,    x+7,  y+31, c); /* 主干                             */
    lcd_fill(x,   y+4,  x+3,  y+20, c); /* 左侧臂 + 顶帽                   */
    lcd_fill(x+8, y+2,  x+11, y+18, c); /* 右侧臂 + 顶帽                   */
}

static void draw_cactus(int16_t x, int16_t y, uint8_t type, uint16_t c)
{
    if (x >= (int16_t)lcddev.width) return; /* 完全在屏幕外，不绘制         */
    switch (type) {
        case 0:  draw_cact_small(x,    y,    c); break;
        case 1:  draw_cact_tall (x,    y,    c); break;
        default:
            /* 双株：左侧高株 + 右侧矮株（向下偏移 8 像素）               */
            draw_cact_tall (x,    y,    c);
            draw_cact_small(x+14, y+8,  c);
            break;
    }
}

/* ===========================================================================
 * 抬头显示（HUD）绘制
 * =========================================================================*/
static void draw_ground(void)
{
    lcd_draw_hline(0, G.ground_y,   lcddev.width, GND_CLR);
    lcd_draw_hline(0, G.ground_y+1, lcddev.width, GND_CLR);
    lcd_draw_hline(0, G.ground_y+2, lcddev.width, GND_CLR);
}

static void draw_score(void)
{
    lcd_fill(0, 0, lcddev.width-1, 18, BG_CLR);
    lcd_show_chinese_str(2, 2, DINO_STR_SC, TXT_CLR);
    lcd_show_num(26, 2, G.score, 5, 16, TXT_CLR);
    uint16_t hx = lcddev.width / 2;
    lcd_show_chinese_str(hx, 2, DINO_STR_HI, TXT_CLR);
    lcd_show_num(hx + 24, 2, G.hi_score, 5, 16, TXT_CLR);
}

static void show_message(const uint8_t *top, const uint8_t *bot)
{
    uint16_t cx = (lcddev.width  > 120) ? lcddev.width/2  - 70 : 0;
    uint16_t cy = (lcddev.height > 100) ? lcddev.height/2 - 30 : 30;
    lcd_fill(cx, cy-4, cx+160, cy+56, BG_CLR);
    lcd_show_chinese_str(cx, cy,    top, RED);
    lcd_show_chinese_str(cx, cy+24, bot, TXT_CLR);
}

/* ===========================================================================
 * 碰撞检测（AABB，恐龙边界内缩 4 像素以提升游戏体验）
 * =========================================================================*/
static uint8_t collide(void)
{
    uint8_t i;
    int16_t dw = G.dino.ducking ? DINO_W_D : DINO_W_S;
    int16_t dh = G.dino.ducking ? DINO_H_D : DINO_H_S;

    int16_t dx1 = DINO_X        + 4;
    int16_t dy1 = G.dino.y      + 4;
    int16_t dx2 = DINO_X + dw   - 5;
    int16_t dy2 = G.dino.y + dh - 2;

    for (i = 0; i < MAX_CACTI; i++) {
        if (!G.cacti[i].active) continue;
        int16_t cx1 = G.cacti[i].x                         + 2;
        int16_t cy1 = G.ground_y - (int16_t)CD[G.cacti[i].type].h;
        int16_t cx2 = G.cacti[i].x + CD[G.cacti[i].type].w - 3;
        int16_t cy2 = G.ground_y                            - 1;

        if (dx1 < cx2 && dx2 > cx1 && dy1 < cy2 && dy2 > cy1) return 1;
    }
    return 0;
}

/* ===========================================================================
 * 游戏初始化 / 重置
 * =========================================================================*/
static void game_reset(void)
{
    uint8_t i;

    G.state   = GAME_RUN;
    G.frame   = 0;
    G.score   = 0;
    G.speed   = SPEED_INIT;

    /* 恐龙站立在地面上，初始状态 */
    G.dino.y         = G.ground_y - DINO_H_S;
    G.dino.prev_y    = G.dino.y;
    G.dino.vy_fp8    = 0;
    G.dino.jumping   = 0;
    G.dino.ducking   = 0;
    G.dino.prev_duck = 0;
    G.dino.anim      = 0;
    G.dino.anim_cnt  = 0;

    for (i = 0; i < MAX_CACTI; i++) {
        G.cacti[i].active = 0;
        G.cacti[i].prev_x = -200; /* 哨兵值：精灵不在屏幕上               */
    }

    /* 第一个仙人掌在足够长的间距后出现 */
    G.spawn_cnt = (SPAWN_MIN_PX + rng(SPAWN_MAX_PX - SPAWN_MIN_PX)) / G.speed;
    if (G.spawn_cnt < 40) G.spawn_cnt = 40;
}

static void spawn_cactus(void)
{
    uint8_t i;
    for (i = 0; i < MAX_CACTI; i++) {
        if (!G.cacti[i].active) {
            G.cacti[i].active = 1;
            G.cacti[i].x      = (int16_t)lcddev.width; /* 从屏幕右侧外出现  */
            G.cacti[i].prev_x = (int16_t)lcddev.width;
            G.cacti[i].type   = rng(3);
            return;
        }
    }
}

/* ===========================================================================
 * dino_game_run - 公共入口函数（永不返回）
 * =========================================================================*/
void dino_game_run(void)
{
    uint8_t  key, i;
    uint8_t  do_render;
    uint32_t t0, dt;

    /* 地面线：距屏幕底部 40 像素 */
    G.ground_y = lcddev.height - 40;
    G.hi_score = 0;
    G.state    = GAME_START;

    /* ------------------------------------------------------------------
     * 开始界面
     * ----------------------------------------------------------------*/
    lcd_clear(BG_CLR);
    draw_ground();

    /* 绘制静态预览：左侧恐龙，前方两个仙人掌 */
    draw_dino(DINO_X,       G.ground_y - DINO_H_S, 0, 0, DINO_CLR);
    draw_cactus(DINO_X+100, G.ground_y - CD[1].h,  1, CACT_CLR);
    draw_cactus(DINO_X+160, G.ground_y - CD[0].h,  0, CACT_CLR);

    uint16_t msg_x = (lcddev.width > 200) ? lcddev.width/2 - 70 : 10;
    uint16_t msg_y = 30;
    lcd_show_chinese_str(msg_x + 16, msg_y,    DINO_STR_TITLE, RED);
    lcd_show_chinese_str(msg_x,      msg_y+24, DINO_STR_JUMP,  TXT_CLR);
    lcd_show_chinese_str(msg_x,      msg_y+44, DINO_STR_DUCK,  TXT_CLR);
    lcd_show_chinese_str(msg_x,      msg_y+64, DINO_STR_REST,  TXT_CLR);
    lcd_show_chinese_str(msg_x,      msg_y+88, DINO_STR_START, RED);

    /* 等待玩家按下 KEY_UP 键；按 KEY2 返回主菜单 */
    { uint8_t k;
      while (1) {
          k = key_scan(0);
          if (k == WKUP_PRES) break;
          if (k == KEY2_PRES) { lcd_clear(BG_CLR); return; }
          delay_ms(20);
      }
    }

    /* ------------------------------------------------------------------
     * 游戏主循环
     * ----------------------------------------------------------------*/
    game_reset();
    lcd_clear(BG_CLR);
    draw_ground();
    draw_score();
    draw_dino(DINO_X, G.dino.y, 0, 0, DINO_CLR);

    while (1)
    {
        t0 = HAL_GetTick();
        do_render = 1;

        /* ---- 输入处理 ----------------------------------------------- */
        key = key_scan(0);
        /* 俯冲需要长按按键，直接读 GPIO（KEY0 低电平有效）*/
        uint8_t is_duck = (HAL_GPIO_ReadPin(KEY0_GPIO_PORT, KEY0_GPIO_PIN)
                           == GPIO_PIN_RESET);

        /* ---- 游戏逻辑更新（仅在运行状态下执行） --------------------- */
        if (G.state == GAME_RUN)
        {
            /* 按键控制 */
            if (key == WKUP_PRES && !G.dino.jumping) {
                G.dino.vy_fp8  = JUMP_VY_FP8;
                G.dino.jumping = 1;
            }
            G.dino.ducking = (!G.dino.jumping) ? is_duck : 0;

            /* 物理运算 */
            if (G.dino.jumping || G.dino.vy_fp8 != 0) {
                G.dino.vy_fp8 += GRAVITY_FP8;
                G.dino.y      += G.dino.vy_fp8 / 8;

                int16_t floor_y = G.ground_y
                    - (int16_t)(G.dino.ducking ? DINO_H_D : DINO_H_S);
                if (G.dino.y >= floor_y) {
                    G.dino.y       = floor_y;
                    G.dino.vy_fp8  = 0;
                    G.dino.jumping = 0;
                }
            }

            /* 腿部动画：落地时每 5 帧切换一次 */
            if (!G.dino.jumping) {
                if (++G.dino.anim_cnt >= 5) {
                    G.dino.anim_cnt = 0;
                    G.dino.anim    ^= 1;
                }
            }

            /* 仙人掌生成倒计时 */
            if (G.spawn_cnt > 0) {
                G.spawn_cnt--;
            } else {
                spawn_cactus();
                uint16_t gap = SPAWN_MIN_PX + rng(SPAWN_MAX_PX - SPAWN_MIN_PX);
                G.spawn_cnt = gap / G.speed;
                if (G.spawn_cnt < 18) G.spawn_cnt = 18; /* 最小间隔保障    */
            }

            /* 仙人掌向左移动 */
            for (i = 0; i < MAX_CACTI; i++) {
                if (!G.cacti[i].active) continue;
                G.cacti[i].x -= (int16_t)G.speed;
                if (G.cacti[i].x + (int16_t)CD[G.cacti[i].type].w < 0)
                    G.cacti[i].active = 0;
            }

            /* 碰撞检测 */
            if (collide()) {
                G.state = GAME_DEAD;
                if (G.score > G.hi_score) G.hi_score = G.score;

                /* 死亡动画：恐龙闪烁红色两次 */
                draw_dino(DINO_X, G.dino.y, G.dino.anim, G.dino.ducking, DEAD_CLR);
                delay_ms(150);
                draw_dino(DINO_X, G.dino.y, G.dino.anim, G.dino.ducking, BG_CLR);
                delay_ms(80);
                draw_dino(DINO_X, G.dino.y, G.dino.anim, G.dino.ducking, DEAD_CLR);

                draw_score();
                show_message(DINO_STR_OVER, DINO_STR_RETRY);
                do_render = 0; /* 本帧跳过正常渲染                          */
            }

            /* 得分与难度递增 */
            G.frame++;
            if (G.frame % 6 == 0) {
                G.score++;
                if ((G.score % SCORE_SPEED_UP == 0) && G.speed < SPEED_MAX)
                    G.speed++;
            }

            /* ---- 渲染：先擦除旧位置，再绘制新位置 ------------------- */
            if (do_render)
            {
                /* 在旧位置擦除恐龙 */
                {
                    uint16_t ew = G.dino.prev_duck ? DINO_W_D : DINO_W_S;
                    uint16_t eh = G.dino.prev_duck ? DINO_H_D : DINO_H_S;
                    erase(DINO_X, G.dino.prev_y, ew, eh);
                }

                /* 在旧位置擦除各仙人掌 */
                for (i = 0; i < MAX_CACTI; i++) {
                    if (G.cacti[i].prev_x > -100) {
                        /* 擦除范围加宽 speed+2，消除运动拖影              */
                        erase(G.cacti[i].prev_x,
                              G.ground_y - (int16_t)CD[G.cacti[i].type].h,
                              (uint16_t)(CD[G.cacti[i].type].w + G.speed + 2),
                              CD[G.cacti[i].type].h);
                    }
                }

                /* 在新位置绘制恐龙 */
                draw_dino(DINO_X, G.dino.y,
                          G.dino.anim, G.dino.ducking, DINO_CLR);
                G.dino.prev_y    = G.dino.y;
                G.dino.prev_duck = G.dino.ducking;

                /* 在新位置绘制各仙人掌 */
                for (i = 0; i < MAX_CACTI; i++) {
                    if (G.cacti[i].active) {
                        draw_cactus(G.cacti[i].x,
                                    G.ground_y - (int16_t)CD[G.cacti[i].type].h,
                                    G.cacti[i].type, CACT_CLR);
                        G.cacti[i].prev_x = G.cacti[i].x;
                    } else {
                        G.cacti[i].prev_x = -200;
                    }
                }

                /* 重新绘制地面线（可能被填充函数擦除）*/
                draw_ground();

                /* 每 15 帧刷新得分显示（约 600 ms）*/
                if (G.frame % 15 == 0) draw_score();

                /* LED0 心跳：每秒翻转一次 */
                if (G.frame % 25 == 0) LED0_TOGGLE();
            }
        }
        else if (G.state == GAME_DEAD)
        {
            /* 等待重新开始按键 */
            if (key == WKUP_PRES || key == KEY1_PRES) {
                lcd_clear(BG_CLR);
                draw_ground();
                game_reset();
                draw_dino(DINO_X, G.dino.y, 0, 0, DINO_CLR);
                draw_score();
            }
            /* KEY2：退出游戏，返回主菜单 */
            if (key == KEY2_PRES) { lcd_clear(BG_CLR); return; }
            delay_ms(20);
        }

        /* ---- 帧率限制 ----------------------------------------------- */
        dt = HAL_GetTick() - t0;
        if (dt < FRAME_MS) delay_ms(FRAME_MS - (uint16_t)dt);
    }
}
