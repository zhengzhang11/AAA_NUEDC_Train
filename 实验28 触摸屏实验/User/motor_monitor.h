/**
 * @file  motor_monitor.h
 * @brief Motor speed monitor - sliding-window scope display via USART3
 *        USART3: PB11=RX (remove P2 jumper for direct MCU-to-MCU wiring)
 *        Frame format received: $<target>,<actual>\n  (rad/s, float)
 *        All comments ASCII-only (Keil GBK-safe)
 */
#ifndef __MOTOR_MONITOR_H
#define __MOTOR_MONITOR_H

#include "./SYSTEM/sys/sys.h"

/**
 * @brief  Motor monitor main entry - called from menu, returns on [BACK] touch.
 */
void motor_monitor_run(void);

#endif /* __MOTOR_MONITOR_H */
