/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* The console's terminal settings: raw mode, VMIN / VTIME reads with nothing typed, and restoring. */

#include <stdio.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static long elapsed_ms(const struct timespec *since)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);

    return (now.tv_sec - since->tv_sec) * 1000 + (now.tv_nsec - since->tv_nsec) / 1000000;
}

int main(void)
{
    struct termios saved;
    struct termios raw;

    if (tcgetattr(0, &saved) != 0)
    {
        printf("tcgetattr failed\n");

        return 1;
    }

    printf("canonical %d echo %d signals %d\n", (saved.c_lflag & ICANON) != 0,
           (saved.c_lflag & ECHO) != 0, (saved.c_lflag & ISIG) != 0);

    raw = saved;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(0, TCSANOW, &raw) != 0)
    {
        printf("tcsetattr failed\n");

        return 1;
    }

    struct termios check;

    tcgetattr(0, &check);
    printf("raw: canonical %d echo %d vmin %d\n", (check.c_lflag & ICANON) != 0,
           (check.c_lflag & ECHO) != 0, check.c_cc[VMIN]);

    char c;
    struct timespec start;

    /* VMIN = 0, VTIME = 0: returns at once with nothing. */
    clock_gettime(CLOCK_MONOTONIC, &start);
    printf("poll read: %d, quick %d\n", (int)read(0, &c, 1), elapsed_ms(&start) < 100);

    /* VMIN = 0, VTIME = 3: waits 0.3 s for a key. */
    raw.c_cc[VTIME] = 3;
    tcsetattr(0, TCSANOW, &raw);
    clock_gettime(CLOCK_MONOTONIC, &start);

    int n = (int)read(0, &c, 1);
    long waited = elapsed_ms(&start);

    printf("timed read: %d, waited about 300 ms %d\n", n, waited >= 280 && waited < 1500);

    tcsetattr(0, TCSANOW, &saved);
    tcgetattr(0, &check);
    printf("restored: canonical %d echo %d\n", (check.c_lflag & ICANON) != 0,
           (check.c_lflag & ECHO) != 0);

    return 0;
}
