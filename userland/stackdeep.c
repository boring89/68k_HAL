/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* The stack grows on demand: deep recursion, and system calls into stack pages never touched before. */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static long depth(int n)
{
    volatile char frame[4096]; /* one page per call */

    frame[0] = (char)n;
    frame[4095] = (char)(n + 1);

    return n == 0 ? 0 : depth(n - 1) + frame[0] - frame[0] + 1;
}

int main(void)
{
    printf("recursion to %ld levels\n", depth(300)); /* about 1.2 MiB of stack, on top of the 600 KB buffer below: under the 2 MiB limit */

    /* A big buffer on the stack that the program has not touched: the kernel writes into it. */
    char big[600000];
    int fd = open("/data/message.txt", O_RDONLY);
    ssize_t n = read(fd, big + 300000, 100);

    close(fd);
    printf("read %s into untouched stack pages\n", n > 0 ? "a file" : "nothing");

    memset(big, 7, sizeof(big));

    long sum = 0;

    for (size_t i = 0; i < sizeof(big); i += 4096)
        sum += big[i];

    printf("touched %ld pages\n", sum / 7);

    return 0;
}
