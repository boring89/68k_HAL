/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Interactive: prints the keyboard and mouse events of /dev/input/event0 until the Q key is pressed. */

#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int fd = open("/dev/input/event0", O_RDONLY);

    if (fd < 0)
    {
        printf("cannot open /dev/input/event0\n");

        return 1;
    }

    printf("reading events, Q quits\n");
    fflush(stdout);

    for (;;)
    {
        struct input_event event;

        if (read(fd, &event, sizeof(event)) != (ssize_t)sizeof(event))
            break;

        if (event.type == EV_KEY)
            printf("key %u %d\n", event.code, event.value);
        else if (event.type == EV_REL)
            printf("rel %u %d\n", event.code, event.value);
        else
            continue;

        fflush(stdout);

        if (event.type == EV_KEY && event.code == KEY_Q && event.value == 1)
            break;
    }

    close(fd);

    return 0;
}
