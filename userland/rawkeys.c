/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Interactive: prints the bytes of every key until 'q'. Arrow and function keys are escape sequences. */

#include <stdio.h>
#include <termios.h>
#include <unistd.h>

int main(void)
{
    struct termios saved;
    struct termios raw;

    if (tcgetattr(0, &saved) != 0)
    {
        printf("not a terminal\n");

        return 1;
    }

    raw = saved;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);

    printf("raw mode, press keys, q quits\r\n");
    fflush(stdout);

    for (;;)
    {
        unsigned char c;

        if (read(0, &c, 1) != 1)
            break;

        printf("%u ", c);
        fflush(stdout);

        if (c == 'q')
            break;
    }

    tcsetattr(0, TCSAFLUSH, &saved);
    printf("\ndone\n");

    return 0;
}
