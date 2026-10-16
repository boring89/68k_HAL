/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "rtc_decode.h"

static bool from_bcd(uint8_t value, uint8_t *out)
{
    if ((value & 0x0F) > 9 || (value >> 4) > 9)
        return false;

    *out = (uint8_t)((value >> 4) * 10 + (value & 0x0F));

    return true;
}

static bool leap_year(unsigned int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static unsigned int days_in_month(unsigned int year, unsigned int month)
{
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};

    return days[month - 1] + (month == 2 && leap_year(year) ? 1 : 0);
}

bool rtc_decode(const rtc_registers_t *regs, hal_datetime_t *time)
{
    bool binary = regs->status_b & RTC_STATUS_B_BINARY;
    bool twenty_four = regs->status_b & RTC_STATUS_B_24_HOUR;

    uint8_t pm = 0;
    uint8_t hour_raw = regs->hour;

    if (!twenty_four)
    {
        pm = hour_raw & RTC_HOUR_PM;
        hour_raw &= (uint8_t)~RTC_HOUR_PM;
    }

    uint8_t second = regs->second, minute = regs->minute, hour = hour_raw;
    uint8_t day = regs->day, month = regs->month, year = regs->year;
    uint8_t century = regs->century;

    if (!binary)
    {
        if (!from_bcd(regs->second, &second) ||
            !from_bcd(regs->minute, &minute) ||
            !from_bcd(hour_raw, &hour) || !from_bcd(regs->day, &day) ||
            !from_bcd(regs->month, &month) || !from_bcd(regs->year, &year))
            return false;

        if (century != 0 && !from_bcd(regs->century, &century))
            century = 0;
    }

    if (!twenty_four)
    {
        /* 12 AM is hour 0 and 12 PM is hour 12. */
        if (hour < 1 || hour > 12)
            return false;

        hour = (uint8_t)(hour % 12 + (pm ? 12 : 0));
    }

    unsigned int full_year;

    if (century >= 19 && century <= 21)
        full_year = century * 100u + year;
    else
        full_year = (year < 70 ? 2000u : 1900u) + year;

    if (year > 99 || second > 59 || minute > 59 || hour > 23 || month < 1 ||
        month > 12 || day < 1 || day > days_in_month(full_year, month))
        return false;

    time->year = (uint16_t)full_year;
    time->month = month;
    time->day = day;
    time->hour = hour;
    time->minute = minute;
    time->second = second;

    return true;
}
