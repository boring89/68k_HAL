/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "platform/pc/rtc/rtc_decode.h"

#define BCD_24H 0x02
#define BIN_24H 0x06
#define BCD_12H 0x00

static void test_bcd_24_hour(void)
{
    /* 2026-10-16 08:05:09, century register 0x20. */
    rtc_registers_t regs = {0x09, 0x05, 0x08, 0x16, 0x10, 0x26, 0x20, BCD_24H};
    hal_datetime_t t;

    TEST_ASSERT(rtc_decode(&regs, &t));
    TEST_ASSERT_EQ_INT(2026, t.year);
    TEST_ASSERT_EQ_INT(10, t.month);
    TEST_ASSERT_EQ_INT(16, t.day);
    TEST_ASSERT_EQ_INT(8, t.hour);
    TEST_ASSERT_EQ_INT(5, t.minute);
    TEST_ASSERT_EQ_INT(9, t.second);
}

static void test_binary(void)
{
    rtc_registers_t regs = {59, 30, 23, 31, 12, 99, 0, BIN_24H};
    hal_datetime_t t;

    /* No century register: 99 means 1999. */
    TEST_ASSERT(rtc_decode(&regs, &t));
    TEST_ASSERT_EQ_INT(1999, t.year);
    TEST_ASSERT_EQ_INT(23, t.hour);
    TEST_ASSERT_EQ_INT(59, t.second);

    regs.year = 4; /* 2004 */
    TEST_ASSERT(rtc_decode(&regs, &t));
    TEST_ASSERT_EQ_INT(2004, t.year);
}

static void test_12_hour(void)
{
    hal_datetime_t t;
    rtc_registers_t regs = {0, 0, 0x12, 0x01, 0x01, 0x26, 0x20, BCD_12H};

    /* 12 AM = hour 0, 12 PM = hour 12, 1 PM = 13, 11 PM = 23. */
    TEST_ASSERT(rtc_decode(&regs, &t));
    TEST_ASSERT_EQ_INT(0, t.hour);
    regs.hour = 0x12 | RTC_HOUR_PM;
    TEST_ASSERT(rtc_decode(&regs, &t));
    TEST_ASSERT_EQ_INT(12, t.hour);
    regs.hour = 0x01 | RTC_HOUR_PM;
    TEST_ASSERT(rtc_decode(&regs, &t));
    TEST_ASSERT_EQ_INT(13, t.hour);
    regs.hour = 0x11 | RTC_HOUR_PM;
    TEST_ASSERT(rtc_decode(&regs, &t));
    TEST_ASSERT_EQ_INT(23, t.hour);
    regs.hour = 0x09;
    TEST_ASSERT(rtc_decode(&regs, &t));
    TEST_ASSERT_EQ_INT(9, t.hour);

    /* 12-hour binary mode: PM flag still in bit 7. */
    regs.status_b = RTC_STATUS_B_BINARY;
    regs.second = 1; regs.minute = 2; regs.day = 3; regs.month = 4;
    regs.year = 26; regs.century = 0;
    regs.hour = 5 | RTC_HOUR_PM;
    TEST_ASSERT(rtc_decode(&regs, &t));
    TEST_ASSERT_EQ_INT(17, t.hour);
    TEST_ASSERT_EQ_INT(2026, t.year);

    regs.hour = 0;
    TEST_ASSERT(!rtc_decode(&regs, &t)); /* hour 0 does not exist in 12-hour mode */
    regs.hour = 13;
    TEST_ASSERT(!rtc_decode(&regs, &t));
}

static void test_rejects(void)
{
    hal_datetime_t t;
    rtc_registers_t good = {0, 0, 0, 0x28, 0x02, 0x24, 0x20, BCD_24H};

    /* 2024 is a leap year: Feb 29 is valid, Feb 30 is not. */
    good.day = 0x29;
    TEST_ASSERT(rtc_decode(&good, &t));
    good.day = 0x30;
    TEST_ASSERT(!rtc_decode(&good, &t));

    /* 2100 is not a leap year, 2000 is. */
    good.century = 0x21; good.year = 0x00; good.day = 0x29;
    TEST_ASSERT(!rtc_decode(&good, &t));
    good.century = 0x20;
    TEST_ASSERT(rtc_decode(&good, &t));
    TEST_ASSERT_EQ_INT(2000, t.year);

    /* Not valid BCD, and fields out of range. */
    rtc_registers_t bad = good;
    bad.second = 0x6A;
    TEST_ASSERT(!rtc_decode(&bad, &t));
    bad = good; bad.second = 0x60;
    TEST_ASSERT(!rtc_decode(&bad, &t));
    bad = good; bad.minute = 0x60;
    TEST_ASSERT(!rtc_decode(&bad, &t));
    bad = good; bad.hour = 0x24;
    TEST_ASSERT(!rtc_decode(&bad, &t));
    bad = good; bad.month = 0x13;
    TEST_ASSERT(!rtc_decode(&bad, &t));
    bad = good; bad.month = 0x00;
    TEST_ASSERT(!rtc_decode(&bad, &t));
    bad = good; bad.day = 0x00;
    TEST_ASSERT(!rtc_decode(&bad, &t));
}

int main(void)
{
    TEST_RUN(test_bcd_24_hour);
    TEST_RUN(test_binary);
    TEST_RUN(test_12_hour);
    TEST_RUN(test_rejects);

    return TEST_REPORT("rtc");
}
