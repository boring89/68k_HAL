/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_RTC_RTC_DECODE_H
#define PLATFORM_PC_RTC_RTC_DECODE_H

#include "hal/clock.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * The CMOS RTC registers as read from the chip (OSDev Wiki "CMOS", "RTC";
 * Motorola MC146818A data sheet).
 */
#define RTC_STATUS_B_24_HOUR 0x02 /* bit 1: hours are 0-23, else 1-12 + PM flag */
#define RTC_STATUS_B_BINARY 0x04  /* bit 2: values are binary, else BCD */
#define RTC_HOUR_PM 0x80          /* bit 7 of the hour register in 12-hour mode */

typedef struct
{
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;    /* two digits */
    uint8_t century; /* register 0x32, or 0 if the chip has none */
    uint8_t status_b;
} rtc_registers_t;

/*
 * Convert raw registers (BCD or binary, 12 or 24 hour) to a date and time.
 * Without a usable century register (0, or not 19-21) years 00-69 are
 * 2000-2069 and 70-99 are 1970-1999. Returns false if a field is out of
 * range (including a day past the end of its month).
 */
bool rtc_decode(const rtc_registers_t *regs, hal_datetime_t *time);

#endif
