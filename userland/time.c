/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Time: the clock moves forward, sleep waits, the date looks plausible. */

#include <stdio.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    struct timespec a;
    struct timespec b;

    clock_gettime(CLOCK_MONOTONIC, &a);
    usleep(200000);
    clock_gettime(CLOCK_MONOTONIC, &b);

    long elapsed_ms = (long)(b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;

    printf("slept: %s\n", elapsed_ms >= 190 && elapsed_ms < 400 ? "ok" : "wrong");

    time_t now = time(NULL);

    printf("year: %s\n", now > 1700000000 ? "ok" : "wrong");

    return 0;
}
