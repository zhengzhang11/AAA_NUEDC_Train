/**
 * @file  oscilloscope.h
 * @brief Software oscilloscope with FFT for ATK-Explorer STM32F407 V3.
 *        Portrait mode (240x320).  Returns when [BACK] is touched.
 */
#ifndef __OSCILLOSCOPE_H
#define __OSCILLOSCOPE_H

#include "./SYSTEM/sys/sys.h"

void oscilloscope_run(void);

#endif /* __OSCILLOSCOPE_H */
