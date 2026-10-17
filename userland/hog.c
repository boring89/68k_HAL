/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Computes for ever and never calls the kernel: only Ctrl-C (through preemption) stops it. */

int main(void)
{
    for (volatile unsigned long i = 0;; i++)
        ;
}
