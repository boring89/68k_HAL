/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Programs must not be able to hurt the kernel: touch memory that is not theirs. */

#include <stdio.h>

int main(void)
{
    volatile int *kernel_memory = (volatile int *)0x00100000;

    printf("before\n");
    fflush(stdout);
    *kernel_memory = 1;
    printf("after (never printed)\n");

    return 0;
}
