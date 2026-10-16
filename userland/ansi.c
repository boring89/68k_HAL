/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Escape sequences on the console: the self test replaces the screen with a
 * recording one and checks where the letters and colours ended up.
 *
 * Row 1: "AB" in the default colours, "G" in bright green; X at row 3 column 5 in red.
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
    struct winsize size;

    if (!isatty(1) || ioctl(1, TIOCGWINSZ, &size) != 0)
    {
        printf("not a terminal\n");

        return 1;
    }

    printf("\x1b[2J\x1b[3;5H\x1b[31mX\x1b[0m\x1b[1;1HAB\x1b[1;32mG\x1b[0m\r\n");
    printf("size %dx%d\n", size.ws_row, size.ws_col);

    return 0;
}
