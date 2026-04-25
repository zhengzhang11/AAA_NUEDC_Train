/**
 * @file  video_player.h
 * @brief AVI (MJPEG + PCM) video player module for ATK-Explorer STM32F407.
 *        Plays files from SD card /videos directory.
 *        Audio via ES8388 codec (I2S2 + DMA1 Stream4).
 *        Video decoded by MJPEG software decoder from MJPEG middleware.
 *        Touch UI: dropdown file list, seek bar, volume slider, exit.
 *        All comments pure ASCII (Keil GBK-safe).
 *
 * SD card layout required:
 *   0:/videos/clip.avi   -- AVI: MJPEG video + PCM s16le audio
 *
 * AVI encode recipe (FFmpeg):
 *   ffmpeg -i in.mp4 -vcodec mjpeg -q:v 3 -vf "scale=240:176,fps=20"
 *          -acodec pcm_s16le -ar 22050 -ac 1  out.avi
 *
 * Dependencies (copy from exp45 to this project):
 *   BSP:         SDIO, ES8388, I2S, SRAM, TIMER/btim
 *   Middlewares: FATFS, MALLOC, MJPEG (avi.h + mjpeg.h + mjpegdec.c)
 */
#ifndef __VIDEO_PLAYER_H
#define __VIDEO_PLAYER_H

#include "./SYSTEM/sys/sys.h"

/* ---- SD card path ------------------------------------------------- */
#define VP_VIDEO_DIR        "0:/videos"

/* ---- Buffer sizes ------------------------------------------------- */
#define VP_AUDIO_BUF_SZ     (1024u * 5u)    /* one audio DMA slot: 5 KB  */
#define VP_VIDEO_BUF_SZ     (1024u * 60u)   /* MJPEG frame buffer: 60 KB */

/* ---- File list ---------------------------------------------------- */
#define VP_MAX_FILES        32u
#define VP_FNAME_LEN        48u     /* max chars per filename (display) */

/* ---- Volume (ES8388 headphone register: 0 = max, 33 ~ mute) ------- */
#define VP_VOL_MAX          33u
#define VP_VOL_DEFAULT      20u

/* ---- Public API --------------------------------------------------- */
void video_player_run(void);   /* enter player; returns on EXIT touch */

#endif /* __VIDEO_PLAYER_H */
