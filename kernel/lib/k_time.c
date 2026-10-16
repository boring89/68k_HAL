/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "k_time.h"

int64_t k_epoch_from_civil(int year, int month, int day, int hour, int minute, int second)
{
    /* Count years from March so the leap day is the last day of the year. */
    int y = month <= 2 ? year - 1 : year;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;                                            /* [0, 399] */
    int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1; /* [0, 365] */
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                    /* [0, 146096] */
    int64_t days = (int64_t)era * 146097 + doe - 719468;

    return days * 86400 + hour * 3600 + minute * 60 + second;
}
