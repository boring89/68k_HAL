/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_LIB_K_TIME_H
#define KERNEL_LIB_K_TIME_H

#include <stdint.h>

/*
 * Seconds since 1970-01-01 00:00:00 UTC (the Unix epoch) of a calendar
 * date and time in UTC, for the Gregorian calendar. Uses the
 * days-from-civil algorithm (H. Hinnant, "chrono-Compatible Low-Level
 * Date Algorithms"). month is 1-12, day 1-31. Years before 1970 give
 * negative values.
 */
int64_t k_epoch_from_civil(int year, int month, int day, int hour, int minute, int second);

#endif
