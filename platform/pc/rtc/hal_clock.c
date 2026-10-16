/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/clock.h"

#include "rtc_decode.h"

#include "arch/x86/io/io.h"

#include <stddef.h>

/* CMOS index and data ports and register numbers (OSDev Wiki "CMOS"). */
#define CMOS_INDEX_PORT 0x70
#define CMOS_DATA_PORT 0x71

#define CMOS_REG_SECOND 0x00
#define CMOS_REG_MINUTE 0x02
#define CMOS_REG_HOUR 0x04
#define CMOS_REG_DAY 0x07
#define CMOS_REG_MONTH 0x08
#define CMOS_REG_YEAR 0x09
#define CMOS_REG_STATUS_A 0x0A
#define CMOS_REG_STATUS_B 0x0B
#define CMOS_REG_CENTURY 0x32 /* usual place; the real one is in the ACPI FADT */

#define CMOS_STATUS_A_UPDATE_IN_PROGRESS 0x80

/* An update takes under 2 ms; a bound so a dead chip cannot hang us. */
#define RTC_WAIT_LOOPS 1000000
#define RTC_READ_TRIES 5

static uint8_t cmos_read(uint8_t reg)
{
    /* Bit 7 of the index port disables NMI; leave it clear. */
    outb(CMOS_INDEX_PORT, reg);

    return inb(CMOS_DATA_PORT);
}

static void read_registers(rtc_registers_t *regs)
{
    regs->second = cmos_read(CMOS_REG_SECOND);
    regs->minute = cmos_read(CMOS_REG_MINUTE);
    regs->hour = cmos_read(CMOS_REG_HOUR);
    regs->day = cmos_read(CMOS_REG_DAY);
    regs->month = cmos_read(CMOS_REG_MONTH);
    regs->year = cmos_read(CMOS_REG_YEAR);
    regs->century = cmos_read(CMOS_REG_CENTURY);
    regs->status_b = cmos_read(CMOS_REG_STATUS_B);
}

static bool registers_equal(const rtc_registers_t *a, const rtc_registers_t *b)
{
    return a->second == b->second && a->minute == b->minute &&
           a->hour == b->hour && a->day == b->day && a->month == b->month &&
           a->year == b->year && a->century == b->century &&
           a->status_b == b->status_b;
}

/*
 * The chip updates its registers once a second and the values are
 * inconsistent while it does (UIP set). Read until two reads in a row,
 * each started with UIP clear, agree.
 */
int hal_clock_now(hal_datetime_t *time)
{
    if (time == NULL)
        return -1;

    rtc_registers_t previous;
    rtc_registers_t current;

    for (int try = 0; try < RTC_READ_TRIES; try++)
    {
        int loops = 0;

        while ((cmos_read(CMOS_REG_STATUS_A) &
                CMOS_STATUS_A_UPDATE_IN_PROGRESS) &&
               loops++ < RTC_WAIT_LOOPS)
            ;

        read_registers(&current);

        if (try > 0 && registers_equal(&previous, &current))
            return rtc_decode(&current, time) ? 0 : -1;

        previous = current;
    }

    return -1;
}
