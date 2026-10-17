/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Fork children that wait on a pipe until fork fails (the OS allows 64 processes), then let them all go. */

#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    int gate[2];
    int children = 0;

    pipe(gate);

    for (int i = 0; i < 100; i++)
    {
        pid_t pid = fork();

        if (pid == 0)
        {
            char byte;

            close(gate[1]);
            read(gate[0], &byte, 1); /* until the parent closes its end */
            _exit(0);
        }

        if (pid < 0)
            break;

        children++;
    }

    int error = errno;

    printf("forked children, fork then failed with %s (errno %d, %d children)\n", error == EAGAIN ? "EAGAIN" : "something else", error, children);
    close(gate[1]);
    close(gate[0]);

    int status;
    int collected = 0;

    while (wait(&status) > 0)
        collected++;

    printf("collected all: %s\n", collected == children ? "yes" : "no");

    return 0;
}
