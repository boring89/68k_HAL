/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_CLOCK_H
#define HAL_CLOCK_H

#include <stdint.h>

/*
 * The battery-backed wall clock. Implemented once per platform
 * (platform/<platform>/rtc/hal_clock.c).
 */

typedef struct
{
    uint16_t year;   /* e.g. 2026 */
    uint8_t month;   /* 1-12 */
    uint8_t day;     /* 1-31 */
    uint8_t hour;    /* 0-23 */
    uint8_t minute;  /* 0-59 */
    uint8_t second;  /* 0-59 */
} hal_datetime_t;

/*
 * The current date and time in UTC (the PC platform assumes the RTC is set
 * to UTC, which is what QEMU does by default; Windows-style local-time RTCs
 * are not handled). Returns 0, or -1 if time is NULL or the clock does not
 * hold a valid date.
 */
int hal_clock_now(hal_datetime_t *time);

#endif
