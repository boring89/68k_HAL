/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_PANIC_PANIC_H
#define KERNEL_PANIC_PANIC_H

/*
 * Prints a message to the console (VGA and serial), disables interrupts
 * and halts the CPU. Never returns.
 */
void panic(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));

#define KASSERT(cond)                                           \
    do                                                          \
    {                                                           \
        if (!(cond))                                            \
            panic("assertion failed: %s (%s:%d)",               \
                  #cond, __FILE__, __LINE__);                   \
    } while (0)

#endif
