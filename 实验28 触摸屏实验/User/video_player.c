/**
 * @file  video_player.c
 * @brief AVI (MJPEG + PCM) video player for ATK-Explorer STM32F407 V3.
 *        Touch UI: header tap -> file dropdown, seek bar drag, vol+/-, exit.
 *        Audio : ES8388 codec via I2S2 + DMA1 Stream4 (4-slot circular).
 *        Video : MJPEG software decode, TIM7 frame-rate timing.
 *        SD card: 0:/videos/*.avi
 *
 * All comments pure ASCII (Keil GBK-safe).
 *
 * Encode recipe:
 *   ffmpeg -i in.mp4 -vcodec mjpeg -q:v 3 -vf "scale=240:176,fps=20"
 *          -acodec pcm_s16le -ar 22050 -ac 1 out.avi
 */

#include "video_player.h"
#include "chinese_font.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/TOUCH/touch.h"
#include "./BSP/ES8388/es8388.h"
#include "./BSP/I2S/i2s.h"
#include "./BSP/TIMER/btim.h"
#include "./SYSTEM/delay/delay.h"
#include "./FATFS/source/ff.h"
#include "./FATFS/exfuns/exfuns.h"
#include "./MALLOC/malloc.h"
#include "./MJPEG/mjpeg.h"
#include "./MJPEG/avi.h"
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Layout constants (240x320 portrait)
 * ========================================================================= */
#define VP_HDR_Y        0u
#define VP_HDR_H        28u     /* header bar height                        */

#define VP_VID_Y        28u     /* video display area top                   */
#define VP_VID_H        208u    /* video display area height                */
#define VP_VID_W        240u

#define VP_SEEK_Y       236u    /* seek bar top                             */
#define VP_SEEK_H       18u     /* seek bar height                          */
#define VP_SEEK_KNOB    6u      /* knob half-width                          */

#define VP_CTL_Y        254u    /* control buttons top                      */
#define VP_CTL_H        36u     /* control button height                    */
#define VP_CTL_W        40u     /* each button width (6 buttons x40 = 240)  */

#define VP_STA_Y        290u    /* status bar top                           */
#define VP_STA_H        30u     /* status bar height                        */

/* Control button X offsets */
#define VP_BTN_PREV_X   0u
#define VP_BTN_PLAY_X   40u
#define VP_BTN_NEXT_X   80u
#define VP_BTN_VOLD_X   120u
#define VP_BTN_VOLU_X   160u
#define VP_BTN_EXIT_X   200u

/* Dropdown item height */
#define VP_DD_ITEM_H    24u
#define VP_DD_VISIBLE   8u      /* max visible items in dropdown            */

/* MJPEG decode offset: video 240x176, centred vertically in 208px area    */
/* offx=0, offy = VP_VID_Y + (VP_VID_H - 176) / 2 = 28 + 16 = 44         */
#define VP_MJPEG_OFFX   0u
#define VP_MJPEG_OFFY   44u

/* Color palette */
#define C_HDR_BG        0x0010u     /* very dark blue header                */
#define C_HDR_TXT       YELLOW
#define C_VID_BG        BLACK
#define C_SEEK_BG       0x2104u     /* dark gray track                      */
#define C_SEEK_FILL     0x07E0u     /* green fill                           */
#define C_SEEK_KNOB     WHITE
#define C_CTL_BG        0x18C3u     /* dark button bg                       */
#define C_CTL_TXT       WHITE
#define C_CTL_PRESS     0x8410u     /* gray flash on press                  */
#define C_STA_BG        0x0000u
#define C_STA_TXT       0x7BEFu     /* light gray                           */
#define C_DD_BG         0x0841u     /* very dark dropdown bg                */
#define C_DD_SEL        0x3186u     /* selected item highlight              */
#define C_DD_TXT        WHITE
#define C_DD_BORDER     0x4208u

/* =========================================================================
 * Private state
 * ========================================================================= */
static char    s_fnames[VP_MAX_FILES][VP_FNAME_LEN]; /* file name list      */
static uint32_t s_foff[VP_MAX_FILES];               /* FATFS dir offsets    */
static uint16_t s_fnum;                             /* number of AVI files  */
static uint16_t s_cur;                              /* currently playing    */
static uint8_t  s_vol;                              /* current volume (0=max, 33=mute) */
static uint8_t  s_paused;                           /* playback paused flag */
static uint16_t s_dd_scroll;                        /* dropdown scroll row  */
static uint8_t  s_dd_open;                          /* dropdown visible     */
static uint8_t  s_has_audio;                        /* 1 if current AVI has audio track */

/* 4-slot audio DMA ring */
static volatile uint8_t s_i2s_play_slot;
static uint8_t *s_i2s_buf[4];

/* TIM7 frame-rate flag (defined in btim.c) */
extern __IO uint8_t frameup;
extern uint16_t frame;

/* =========================================================================
 * Audio DMA callback (called from DMA1_Stream4_IRQHandler via i2s_tx_callback)
 * ========================================================================= */
static void vp_audio_dma_cb(void)
{
    s_i2s_play_slot = (uint8_t)((s_i2s_play_slot + 1u) & 3u);

    /* CT bit (bit 19) in DMA CR: 0 = M0AR active, 1 = M1AR active.
     * We update the INACTIVE buffer so it is ready for the next transfer. */
    if (DMA1_Stream4->CR & (1u << 19))
    {
        DMA1_Stream4->M0AR = (uint32_t)s_i2s_buf[s_i2s_play_slot];
    }
    else
    {
        DMA1_Stream4->M1AR = (uint32_t)s_i2s_buf[s_i2s_play_slot];
    }
}

/* =========================================================================
 * File scanning
 * ========================================================================= */
static void vp_scan_files(void)
{
    DIR     dir;
    FILINFO *fi;
    uint8_t  res;
    uint32_t off;

    s_fnum = 0u;

    fi = (FILINFO *)mymalloc(SRAMIN, sizeof(FILINFO));
    if (!fi) return;

    res = f_opendir(&dir, VP_VIDEO_DIR);
    if (res != FR_OK) { myfree(SRAMIN, fi); return; }

    while (s_fnum < VP_MAX_FILES)
    {
        off = dir.dptr;
        res = f_readdir(&dir, fi);
        if (res != FR_OK || fi->fname[0] == 0) break;
        if ((exfuns_file_type(fi->fname) & 0xF0u) == 0x60u)  /* AVI */
        {
            s_foff[s_fnum] = off;
            /* copy filename, truncate to VP_FNAME_LEN-1 */
            strncpy(s_fnames[s_fnum], fi->fname, VP_FNAME_LEN - 1u);
            s_fnames[s_fnum][VP_FNAME_LEN - 1u] = '\0';
            s_fnum++;
        }
    }
    f_closedir(&dir);
    myfree(SRAMIN, fi);
}

/* =========================================================================
 * UI drawing helpers
 * ========================================================================= */

/* --- Header bar --------------------------------------------------------- */
static void vp_draw_header(uint8_t dd_open)
{
    lcd_fill(0u, VP_HDR_Y, 239u, (uint16_t)(VP_HDR_Y + VP_HDR_H - 1u), C_HDR_BG);
    lcd_show_string(4u, (uint16_t)(VP_HDR_Y + 6u), 200u, 16u, 16u,
                    "VIDEO PLAYER", C_HDR_TXT);
    /* dropdown arrow indicator */
    uint16_t ax = 220u, ay = (uint16_t)(VP_HDR_Y + 10u);
    if (dd_open)
    {
        /* up arrow */
        lcd_draw_line(ax, (uint16_t)(ay + 6u), (uint16_t)(ax + 8u), (uint16_t)(ay + 6u), WHITE);
        lcd_draw_line((uint16_t)(ax + 4u), ay, ax, (uint16_t)(ay + 6u), WHITE);
        lcd_draw_line((uint16_t)(ax + 4u), ay, (uint16_t)(ax + 8u), (uint16_t)(ay + 6u), WHITE);
    }
    else
    {
        /* down arrow */
        lcd_draw_line(ax, ay, (uint16_t)(ax + 8u), ay, WHITE);
        lcd_draw_line((uint16_t)(ax + 4u), (uint16_t)(ay + 6u), ax, ay, WHITE);
        lcd_draw_line((uint16_t)(ax + 4u), (uint16_t)(ay + 6u), (uint16_t)(ax + 8u), ay, WHITE);
    }
}

/* --- Video background --------------------------------------------------- */
static void vp_draw_video_bg(void)
{
    lcd_fill(0u, VP_VID_Y, 239u, (uint16_t)(VP_VID_Y + VP_VID_H - 1u), C_VID_BG);
}

/* --- Seek bar ----------------------------------------------------------- */
static void vp_draw_seek_bar(uint32_t cur_pos, uint32_t total_size)
{
    /* Track background */
    lcd_fill(0u, VP_SEEK_Y, 239u, (uint16_t)(VP_SEEK_Y + VP_SEEK_H - 1u), C_SEEK_BG);

    if (total_size == 0u) return;

    /* Filled portion */
    uint16_t fill_w = (uint16_t)(((uint32_t)238u * cur_pos) / total_size);
    if (fill_w > 0u)
    {
        lcd_fill(1u, (uint16_t)(VP_SEEK_Y + 6u), fill_w,
                 (uint16_t)(VP_SEEK_Y + VP_SEEK_H - 7u), C_SEEK_FILL);
    }

    /* Knob */
    uint16_t kx = (uint16_t)(1u + fill_w);
    if (kx < VP_SEEK_KNOB) kx = VP_SEEK_KNOB;
    if (kx > (uint16_t)(239u - VP_SEEK_KNOB)) kx = (uint16_t)(239u - VP_SEEK_KNOB);
    lcd_fill((uint16_t)(kx - VP_SEEK_KNOB),  VP_SEEK_Y,
             (uint16_t)(kx + VP_SEEK_KNOB),  (uint16_t)(VP_SEEK_Y + VP_SEEK_H - 1u),
             C_SEEK_KNOB);
}

/* --- Control buttons ---------------------------------------------------- */
static void vp_draw_controls(uint8_t paused, uint8_t vol)
{
    /* background */
    lcd_fill(0u, VP_CTL_Y, 239u, (uint16_t)(VP_CTL_Y + VP_CTL_H - 1u), C_CTL_BG);

    /* divider lines between buttons */
    for (uint8_t i = 1u; i < 6u; i++)
    {
        uint16_t lx = (uint16_t)(i * VP_CTL_W);
        lcd_draw_line(lx, VP_CTL_Y, lx, (uint16_t)(VP_CTL_Y + VP_CTL_H - 1u), C_DD_BORDER);
    }

    /* Button labels */
    uint16_t ty = (uint16_t)(VP_CTL_Y + (VP_CTL_H - 16u) / 2u);

    lcd_show_string((uint16_t)(VP_BTN_PREV_X + 8u),  ty, 24u, 16u, 16u, "<<",  C_CTL_TXT);
    if (paused)
        lcd_show_string((uint16_t)(VP_BTN_PLAY_X + 8u), ty, 24u, 16u, 16u, ">|", C_CTL_TXT);
    else
        lcd_show_string((uint16_t)(VP_BTN_PLAY_X + 8u), ty, 24u, 16u, 16u, "||", C_CTL_TXT);
    lcd_show_string((uint16_t)(VP_BTN_NEXT_X + 8u),  ty, 24u, 16u, 16u, ">>",  C_CTL_TXT);
    lcd_show_string((uint16_t)(VP_BTN_VOLD_X + 6u),  ty, 32u, 16u, 16u, "V-",  C_CTL_TXT);
    lcd_show_string((uint16_t)(VP_BTN_VOLU_X + 6u),  ty, 32u, 16u, 16u, "V+",  C_CTL_TXT);
    lcd_show_string((uint16_t)(VP_BTN_EXIT_X + 4u),  ty, 32u, 16u, 16u, "EXIT", C_CTL_TXT);

    (void)vol;  /* volume is shown in status bar */
}

/* --- Status bar --------------------------------------------------------- */
static void vp_draw_status(FIL *favi, AVI_INFO *info, uint8_t vol)
{
    static uint32_t s_last_sec = 0xFFFFFFFFu;
    uint32_t totsec, cursec;
    char buf[48];

    if (!favi || !info || info->SecPerFrame == 0u) return;

    totsec = (info->SecPerFrame / 1000u) * info->TotalFrame / 1000u;
    cursec = (uint32_t)(((double)favi->fptr / (double)favi->obj.objsize) * (double)totsec);

    if (cursec == s_last_sec) return;   /* avoid unnecessary redraws */
    s_last_sec = cursec;

    lcd_fill(0u, VP_STA_Y, 239u, (uint16_t)(VP_STA_Y + VP_STA_H - 1u), C_STA_BG);

    /* Time: CUR / TOTAL */
    snprintf(buf, sizeof(buf), "%02lu:%02lu / %02lu:%02lu  VOL:%d%%",
             cursec / 60u, cursec % 60u,
             totsec / 60u, totsec % 60u,
             (int)(100 - (vol * 100 / VP_VOL_MAX)));
    lcd_show_string(2u, (uint16_t)(VP_STA_Y + 7u), 236u, 16u, 16u, buf, C_STA_TXT);
}

/* status bar for idle (no file open) */
static void vp_draw_status_idle(uint8_t vol)
{
    char buf[32];
    lcd_fill(0u, VP_STA_Y, 239u, (uint16_t)(VP_STA_Y + VP_STA_H - 1u), C_STA_BG);
    snprintf(buf, sizeof(buf), "Ready  VOL:%d%%",
             (int)(100 - (vol * 100 / VP_VOL_MAX)));
    lcd_show_string(2u, (uint16_t)(VP_STA_Y + 7u), 236u, 16u, 16u, buf, C_STA_TXT);
}

/* --- Dropdown overlay --------------------------------------------------- */
static void vp_draw_dropdown(void)
{
    if (s_fnum == 0u)
    {
        /* no files */
        lcd_fill(0u, VP_VID_Y, 239u,
                 (uint16_t)(VP_VID_Y + VP_DD_ITEM_H - 1u), C_DD_BG);
        lcd_draw_rectangle(0u, VP_VID_Y, 239u,
                           (uint16_t)(VP_VID_Y + VP_DD_ITEM_H - 1u), C_DD_BORDER);
        lcd_show_string(4u, (uint16_t)(VP_VID_Y + 4u), 236u, 16u, 16u,
                        "No .avi files in /videos", C_CTL_TXT);
        return;
    }

    uint16_t visible = (s_fnum < VP_DD_VISIBLE) ? s_fnum : VP_DD_VISIBLE;

    for (uint16_t i = 0u; i < visible; i++)
    {
        uint16_t fi  = (uint16_t)(s_dd_scroll + i);
        uint16_t y0  = (uint16_t)(VP_VID_Y + i * VP_DD_ITEM_H);
        uint16_t y1  = (uint16_t)(y0 + VP_DD_ITEM_H - 1u);
        uint16_t bg  = (fi == s_cur) ? C_DD_SEL : C_DD_BG;

        lcd_fill(0u, y0, 239u, y1, bg);
        lcd_draw_line(0u, y1, 239u, y1, C_DD_BORDER);

        /* item number prefix */
        char prefix[6];
        snprintf(prefix, sizeof(prefix), "%2d: ", (int)(fi + 1));
        lcd_show_string(4u, (uint16_t)(y0 + 4u), 24u, 16u, 16u, prefix, C_DD_TXT);

        /* filename (truncated to fit) */
        lcd_show_string(36u, (uint16_t)(y0 + 4u), 200u, 16u, 16u,
                        s_fnames[fi], C_DD_TXT);
    }

    /* scroll indicator if more files than visible */
    if (s_fnum > VP_DD_VISIBLE)
    {
        char ind[12];
        uint16_t iy = (uint16_t)(VP_VID_Y + visible * VP_DD_ITEM_H);
        lcd_fill(0u, iy, 239u, (uint16_t)(iy + 12u), C_DD_BG);
        snprintf(ind, sizeof(ind), "^v %d/%d", (int)(s_dd_scroll + 1), (int)s_fnum);
        lcd_show_string(90u, (uint16_t)(iy + 1u), 60u, 12u, 12u, ind, C_STA_TXT);
    }
}

/* --- Full screen repaint (idle/between files) --------------------------- */
static void vp_draw_full(void)
{
    vp_draw_header(s_dd_open);
    vp_draw_video_bg();
    vp_draw_seek_bar(0u, 1u);
    vp_draw_controls(s_paused, s_vol);
    vp_draw_status_idle(s_vol);
}

/* =========================================================================
 * Touch hit-test helpers
 * ========================================================================= */
static uint8_t vp_hit(uint16_t px, uint16_t py,
                      uint16_t x,  uint16_t y,
                      uint16_t w,  uint16_t h)
{
    return (px >= x && px < (uint16_t)(x + w) &&
            py >= y && py < (uint16_t)(y + h)) ? 1u : 0u;
}

/* =========================================================================
 * Volume helpers
 * ========================================================================= */
static void vp_vol_apply(void)
{
    es8388_hpvol_set(s_vol);
}

static void vp_vol_up(void)
{
    if (s_vol >= 3u)  s_vol -= 3u;       /* lower register = louder        */
    else              s_vol  = 0u;
    vp_vol_apply();
}

static void vp_vol_down(void)
{
    if (s_vol + 3u <= VP_VOL_MAX) s_vol += 3u;
    else                          s_vol  = VP_VOL_MAX;
    vp_vol_apply();
}

/* =========================================================================
 * Seek to byte position in AVI, decode one preview frame
 * Returns 0 on success, non-zero on error.
 * ========================================================================= */
static uint8_t vp_seek_to(FIL *favi, AVI_INFO *info,
                           uint8_t *mbuf, uint32_t target)
{
    uint32_t br;
    uint16_t offset;

    /* Clamp target */
    if (target > favi->obj.objsize - VP_VIDEO_BUF_SZ)
        target = favi->obj.objsize - VP_VIDEO_BUF_SZ;

    f_lseek(favi, target);
    f_read(favi, mbuf, VP_VIDEO_BUF_SZ, (UINT *)&br);

    /* Find movi or VideoFLAG depending on position */
    if (target == 0u)
        offset = avi_srarch_id(mbuf, VP_VIDEO_BUF_SZ, "movi");
    else
        offset = 0u;

    offset += avi_srarch_id(mbuf + offset, VP_VIDEO_BUF_SZ, info->VideoFLAG);
    avi_get_streaminfo(mbuf + offset);

    f_lseek(favi, target + offset + 8u);

    if (info->StreamID == AVI_VIDS_FLAG)
    {
        f_read(favi, mbuf, info->StreamSize + 8u, (UINT *)&br);
        mjpegdec_decode(mbuf, info->StreamSize);
    }

    return 0u;
}

/* =========================================================================
 * Full seek operation triggered by seek-bar touch during playback pause.
 * tx = touch X coordinate on seek bar (0..239).
 * ========================================================================= */
static void vp_seekbar_touch(FIL *favi, AVI_INFO *info,
                              uint8_t *mbuf, uint16_t tx)
{
    if (favi->obj.objsize == 0u) return;

    uint32_t target = (uint32_t)(((uint32_t)tx * favi->obj.objsize) / 240u);

    if (s_has_audio) { i2s_play_stop(); }
    vp_seek_to(favi, info, mbuf, target);
    vp_draw_seek_bar(favi->fptr, favi->obj.objsize);
    vp_draw_status(favi, info, s_vol);
    if (s_has_audio) { i2s_play_start(); }
}

/* =========================================================================
 * Play one AVI file. Returns:
 *   0   - file ended normally (auto advance)
 *   1   - user pressed EXIT
 *   2   - user pressed PREV (<< button)
 *   3   - user pressed NEXT (>> button)
 * ========================================================================= */
static uint8_t vp_play_avi(const char *path)
{
    uint8_t *framebuf = NULL;
    FIL     *favi     = NULL;
    uint32_t nr;
    uint8_t  res = 0u;
    uint8_t  i2s_save;
    uint8_t  ret = 0u;          /* return code */

    /* ---- Allocate buffers ---- */
    s_i2s_buf[0] = (uint8_t *)mymalloc(SRAMIN, VP_AUDIO_BUF_SZ);
    s_i2s_buf[1] = (uint8_t *)mymalloc(SRAMIN, VP_AUDIO_BUF_SZ);
    s_i2s_buf[2] = (uint8_t *)mymalloc(SRAMIN, VP_AUDIO_BUF_SZ);
    s_i2s_buf[3] = (uint8_t *)mymalloc(SRAMIN, VP_AUDIO_BUF_SZ);
    framebuf      = (uint8_t *)mymalloc(SRAMIN, VP_VIDEO_BUF_SZ);
    favi          = (FIL *)mymalloc(SRAMIN, sizeof(FIL));

    if (!s_i2s_buf[0] || !s_i2s_buf[1] || !s_i2s_buf[2] || !s_i2s_buf[3]
        || !framebuf || !favi)
    {
        ret = 0u;   /* treat as normal end, caller will advance file */
        goto cleanup;
    }

    memset(s_i2s_buf[0], 0, VP_AUDIO_BUF_SZ);
    memset(s_i2s_buf[1], 0, VP_AUDIO_BUF_SZ);
    memset(s_i2s_buf[2], 0, VP_AUDIO_BUF_SZ);
    memset(s_i2s_buf[3], 0, VP_AUDIO_BUF_SZ);

    /* ---- Open file ---- */
    res = f_open(favi, path, FA_READ);
    if (res != 0u) { ret = 0u; goto cleanup; }

    /* ---- Parse AVI header ---- */
    res = f_read(favi, framebuf, VP_VIDEO_BUF_SZ, (UINT *)&nr);
    if (res != 0u || avi_init(framebuf, VP_VIDEO_BUF_SZ) != AVI_OK)
    {
        f_close(favi);
        ret = 0u;
        goto cleanup;
    }

    /* ---- Seek to movi data start ---- */
    uint16_t movi_off = avi_srarch_id(framebuf, VP_VIDEO_BUF_SZ, "movi");
    avi_get_streaminfo(framebuf + movi_off + 4u);
    f_lseek(favi, (uint32_t)(movi_off + 12u));

    /* ---- Init decoders & timers ---- */
    mjpegdec_init(VP_MJPEG_OFFX, VP_MJPEG_OFFY);
    btim_tim7_int_init((uint16_t)(g_avix.SecPerFrame / 100u - 1u), 8400u - 1u);

    s_has_audio = (g_avix.SampleRate > 0u) ? 1u : 0u;
    if (s_has_audio)
    {
        /* Clamp audio DMA slot size to allocated buffer size */
        uint16_t audio_sz = (g_avix.AudioBufSize > VP_AUDIO_BUF_SZ)
                            ? (uint16_t)VP_AUDIO_BUF_SZ
                            : (uint16_t)g_avix.AudioBufSize;
        i2s_init(I2S_STANDARD_PHILIPS, I2S_MODE_MASTER_TX,
                 I2S_CPOL_LOW, I2S_DATAFORMAT_16B_EXTENDED);
        i2s_samplerate_set(g_avix.SampleRate);
        i2s_tx_dma_init(s_i2s_buf[1], s_i2s_buf[2], audio_sz / 2u);
        i2s_tx_callback  = vp_audio_dma_cb;
        s_i2s_play_slot  = 0u;
        i2s_save         = 0u;
        i2s_play_start();
    }
    else
    {
        i2s_save = 0u;
    }

    /* ---- Repaint video area & controls ---- */
    vp_draw_video_bg();
    vp_draw_seek_bar(0u, favi->obj.objsize);
    vp_draw_controls(s_paused, s_vol);
    vp_draw_status(favi, &g_avix, s_vol);

    /* ---- Main decode loop ---- */
    uint8_t  prev_down = 0u;
    uint8_t  seeking   = 0u;   /* 1 when finger is dragging seek bar       */
    uint8_t *pbuf      = framebuf; /* points to buffer holding current chunk  */

    while (1)
    {
        /* ---- Touch poll ---- */
        tp_dev.scan(0);
        uint8_t  down = (tp_dev.sta & TP_PRES_DOWN) ? 1u : 0u;
        uint16_t tx   = tp_dev.x[0];
        uint16_t ty   = tp_dev.y[0];

        if (down && !prev_down)     /* leading edge */
        {
            /* Seek bar drag start */
            if (vp_hit(tx, ty, 0u, VP_SEEK_Y, 240u, VP_SEEK_H))
            {
                seeking = 1u;
                if (!s_paused && s_has_audio) { i2s_play_stop(); }
            }
            /* Control buttons */
            else if (vp_hit(tx, ty, VP_BTN_PREV_X, VP_CTL_Y, VP_CTL_W, VP_CTL_H))
            {
                lcd_fill(VP_BTN_PREV_X, VP_CTL_Y,
                         (uint16_t)(VP_BTN_PREV_X + VP_CTL_W - 1u),
                         (uint16_t)(VP_CTL_Y + VP_CTL_H - 1u), C_CTL_PRESS);
                delay_ms(80u);
                ret = 2u;   /* PREV */
                break;
            }
            else if (vp_hit(tx, ty, VP_BTN_PLAY_X, VP_CTL_Y, VP_CTL_W, VP_CTL_H))
            {
                s_paused ^= 1u;
                if (s_paused) {
                    if (s_has_audio) { i2s_play_stop(); }
                } else {
                    if (s_has_audio) { i2s_play_start(); }
                }
                vp_draw_controls(s_paused, s_vol);
            }
            else if (vp_hit(tx, ty, VP_BTN_NEXT_X, VP_CTL_Y, VP_CTL_W, VP_CTL_H))
            {
                lcd_fill(VP_BTN_NEXT_X, VP_CTL_Y,
                         (uint16_t)(VP_BTN_NEXT_X + VP_CTL_W - 1u),
                         (uint16_t)(VP_CTL_Y + VP_CTL_H - 1u), C_CTL_PRESS);
                delay_ms(80u);
                ret = 3u;   /* NEXT */
                break;
            }
            else if (vp_hit(tx, ty, VP_BTN_VOLD_X, VP_CTL_Y, VP_CTL_W, VP_CTL_H))
            {
                vp_vol_down();
                vp_draw_status(favi, &g_avix, s_vol);
            }
            else if (vp_hit(tx, ty, VP_BTN_VOLU_X, VP_CTL_Y, VP_CTL_W, VP_CTL_H))
            {
                vp_vol_up();
                vp_draw_status(favi, &g_avix, s_vol);
            }
            else if (vp_hit(tx, ty, VP_BTN_EXIT_X, VP_CTL_Y, VP_CTL_W, VP_CTL_H))
            {
                ret = 1u;   /* EXIT */
                break;
            }
            /* Header tap: pause and return to dropdown */
            else if (vp_hit(tx, ty, 0u, VP_HDR_Y, 240u, VP_HDR_H))
            {
                if (!s_paused) {
                    s_paused = 1u;
                    if (s_has_audio) { i2s_play_stop(); }
                }
                ret = 0xFEu;  /* signal: back to state machine for dropdown */
                break;
            }
        }
        else if (down && seeking)
        {
            /* Drag on seek bar: live preview */
            vp_seekbar_touch(favi, &g_avix, framebuf, tx);
        }
        else if (!down && seeking)
        {
            seeking = 0u;
            if (!s_paused && s_has_audio) { i2s_play_start(); }
            vp_draw_controls(s_paused, s_vol);
        }
        prev_down = down;

        /* ---- While paused: just keep polling ---- */
        if (s_paused) { delay_ms(10u); continue; }

        /* ---- AV decode ---- */
        if (g_avix.StreamID == AVI_VIDS_FLAG)
        {
            /* Video frame: read chunk data + 8-byte next-chunk header */
            pbuf = framebuf;
            f_read(favi, pbuf, g_avix.StreamSize + 8u, (UINT *)&nr);
            mjpegdec_decode(pbuf, g_avix.StreamSize);

            /* Wait for TIM7 frame-rate tick (A/V sync gate) */
            while (frameup == 0u);
            frameup = 0u;
            frame++;

            /* Throttle seek bar: update every 20 frames (~1 s) to stop flicker */
            if ((frame % 20u) == 0u)
            {
                vp_draw_seek_bar(favi->fptr, favi->obj.objsize);
            }
        }
        else
        {
            /* Audio chunk -------------------------------------------------- */
            if (s_has_audio)
            {
                i2s_save = (uint8_t)((i2s_save + 1u) & 3u);

                /* Spin-wait until save slot is not the slot just-behind play */
                uint32_t guard;
                do {
                    guard = s_i2s_play_slot;
                    guard = (guard == 0u) ? 3u : (guard - 1u);
                } while (i2s_save == (uint8_t)guard);

                /* CRITICAL FIX: pbuf must track the buffer we just filled    */
                pbuf = s_i2s_buf[i2s_save];
                f_read(favi, pbuf, g_avix.StreamSize + 8u, (UINT *)&nr);
            }
            else
            {
                /* Video-only AVI: read audio chunk into framebuf as scratch  */
                pbuf = framebuf;
                f_read(favi, pbuf, g_avix.StreamSize + 8u, (UINT *)&nr);
            }

            /* Update time display on every audio chunk */
            vp_draw_status(favi, &g_avix, s_vol);
        }

        /* ---- Advance to next chunk header (sits at pbuf+StreamSize) ---- */
        if (avi_get_streaminfo(pbuf + g_avix.StreamSize) != AVI_OK)
        {
            ret = 0u;   /* normal end-of-file */
            break;
        }
    }   /* end decode loop */

    /* ---- Teardown ---- */
    if (s_has_audio) { i2s_play_stop(); }
    TIM7->CR1 &= ~(1u << 0);                            /* disable TIM7    */
    lcd_set_window(0u, 0u, lcddev.width, lcddev.height);/* restore window  */
    mjpegdec_free();
    f_close(favi);

cleanup:
    myfree(SRAMIN, s_i2s_buf[0]);
    myfree(SRAMIN, s_i2s_buf[1]);
    myfree(SRAMIN, s_i2s_buf[2]);
    myfree(SRAMIN, s_i2s_buf[3]);
    s_i2s_buf[0] = s_i2s_buf[1] = s_i2s_buf[2] = s_i2s_buf[3] = NULL;
    myfree(SRAMIN, framebuf);
    myfree(SRAMIN, favi);

    return ret;
}

/* =========================================================================
 * Public entry point
 * ========================================================================= */
void video_player_run(void)
{
    /* ---- Init hardware ---- */
    es8388_init();
    es8388_adda_cfg(1u, 0u);           /* enable DAC, disable ADC           */
    es8388_output_cfg(1u, 1u);         /* both output channels on           */

    s_vol    = VP_VOL_DEFAULT;
    s_paused = 0u;
    s_dd_open    = 0u;
    s_dd_scroll  = 0u;
    s_cur        = 0u;

    vp_vol_apply();

    /* ---- Scan SD card ---- */
    vp_scan_files();

    /* ---- Initial full paint ---- */
    g_back_color = BLACK;
    vp_draw_full();

    /* ---- Open dropdown immediately if files found ---- */
    if (s_fnum > 0u)
    {
        s_dd_open = 1u;
        vp_draw_header(1u);
        vp_draw_dropdown();
    }
    else
    {
        /* No files: show hint and wait for EXIT button */
        lcd_fill(0u, VP_VID_Y, 239u, (uint16_t)(VP_VID_Y + VP_VID_H - 1u), BLACK);
        lcd_show_string(8u,  (uint16_t)(VP_VID_Y + 70u), 224u, 16u, 16u,
                        "SD card: 0:/videos/", RED);
        lcd_show_string(8u,  (uint16_t)(VP_VID_Y + 92u), 224u, 16u, 16u,
                        "No *.avi files found!", RED);
        lcd_show_string(8u,  (uint16_t)(VP_VID_Y + 114u), 224u, 16u, 16u,
                        "Or SD card not ready.", YELLOW);
        lcd_show_string(20u, (uint16_t)(VP_CTL_Y + 10u), 200u, 16u, 16u,
                        "  [ EXIT ]", WHITE);
    }

    /* ============================================================
     * Main state machine
     * STATE 0: dropdown open  (file selection)
     * STATE 1: playing
     * STATE 2: return to main menu (exit)
     * ============================================================ */
    uint8_t state    = (s_fnum > 0u) ? 0u : 0u;  /* stay in UI even if no files */
    uint8_t prev_down = 0u;

    while (state != 2u)
    {
        tp_dev.scan(0);
        uint8_t  down = (tp_dev.sta & TP_PRES_DOWN) ? 1u : 0u;
        uint16_t tx   = tp_dev.x[0];
        uint16_t ty   = tp_dev.y[0];

        /* ---- State 0: dropdown open ------------------------------------ */
        if (state == 0u)
        {
            if (down && !prev_down)
            {
                /* Header: close dropdown */
                if (vp_hit(tx, ty, 0u, VP_HDR_Y, 240u, VP_HDR_H))
                {
                    s_dd_open = 0u;
                    vp_draw_full();     /* redraw without dropdown */
                }
                /* Exit button area */
                else if (vp_hit(tx, ty, VP_BTN_EXIT_X, VP_CTL_Y, VP_CTL_W, VP_CTL_H))
                {
                    state = 2u;         /* exit player */
                }
                /* Dropdown scroll up */
                else if (s_fnum > VP_DD_VISIBLE &&
                         vp_hit(tx, ty, 0u, VP_VID_Y,
                                120u, (uint16_t)(VP_DD_VISIBLE * VP_DD_ITEM_H + 12u)))
                {
                    /* left half of scroll row = scroll up */
                    uint16_t row = (uint16_t)((ty - VP_VID_Y) / VP_DD_ITEM_H);
                    if (row < VP_DD_VISIBLE && (s_dd_scroll + row) < s_fnum)
                    {
                        /* item tap */
                        s_cur = (uint16_t)(s_dd_scroll + row);
                        s_dd_open = 0u;
                        vp_draw_full();
                        s_paused = 0u;
                        state = 1u;     /* go to playing */
                    }
                }
                /* Tap on a dropdown item row */
                else if (vp_hit(tx, ty, 0u, VP_VID_Y, 240u,
                                (uint16_t)(VP_DD_VISIBLE * VP_DD_ITEM_H)))
                {
                    uint16_t row = (uint16_t)((ty - VP_VID_Y) / VP_DD_ITEM_H);
                    uint16_t fi  = (uint16_t)(s_dd_scroll + row);
                    if (fi < s_fnum)
                    {
                        s_cur = fi;
                        s_dd_open = 0u;
                        vp_draw_full();
                        s_paused = 0u;
                        state = 1u;
                    }
                }
                /* Scroll area row taps */
                else if (vp_hit(tx, ty, 0u,
                                (uint16_t)(VP_VID_Y + VP_DD_VISIBLE * VP_DD_ITEM_H),
                                120u, 12u))
                {
                    /* scroll up */
                    if (s_dd_scroll > 0u) s_dd_scroll--;
                    vp_draw_dropdown();
                }
                else if (vp_hit(tx, ty, 120u,
                                (uint16_t)(VP_VID_Y + VP_DD_VISIBLE * VP_DD_ITEM_H),
                                120u, 12u))
                {
                    /* scroll down */
                    if (s_dd_scroll + VP_DD_VISIBLE < s_fnum) s_dd_scroll++;
                    vp_draw_dropdown();
                }
            }
        }

        /* ---- State 1: play file ---------------------------------------- */
        else if (state == 1u)
        {
            /* Build full path */
            char path[64];
            snprintf(path, sizeof(path), "%s/%s", VP_VIDEO_DIR, s_fnames[s_cur]);

            uint8_t res = vp_play_avi(path);

            /* Repaint full screen after playback */
            vp_draw_full();

            if (res == 1u)          /* EXIT pressed */
            {
                state = 2u;
            }
            else if (res == 2u)     /* PREV */
            {
                s_cur = (s_cur == 0u) ? (uint16_t)(s_fnum - 1u) : (uint16_t)(s_cur - 1u);
                state = 1u;         /* loop back */
            }
            else if (res == 3u)     /* NEXT */
            {
                s_cur = (uint16_t)((s_cur + 1u) % s_fnum);
                state = 1u;
            }
            else if (res == 0xFEu)  /* re-open dropdown mid-play */
            {
                s_dd_open = 1u;
                vp_draw_header(1u);
                vp_draw_dropdown();
                state = 0u;
            }
            else                    /* normal end: auto next */
            {
                s_cur = (uint16_t)((s_cur + 1u) % s_fnum);
                state = 1u;
            }
        }

        prev_down = down;
        if (state == 0u) delay_ms(20u);     /* idle polling rate */
    }

    /* ---- Cleanup ---- */
    if (s_has_audio) { i2s_play_stop(); }
    TIM7->CR1 &= ~(1u << 0);
}
