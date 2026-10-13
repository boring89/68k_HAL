/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "panic.h"

#include "kernel/printf/printf.h"

#include "hal/cpu.h"

#ifdef KERNEL_SELFTEST
#include "kernel/test/selftest.h"
#endif

#include <stdarg.h>

void panic(const char *fmt, ...)
{
    hal_cpu_disable_interrupts();

    va_list args;
    va_start(args, fmt);

    kernel_printf("\nKERNEL PANIC: ");
    kernel_vprintf(fmt, args);
    kernel_printf("\n");

    va_end(args);

#ifdef KERNEL_SELFTEST
    /* Fail the test run immediately instead of waiting for the timeout. */
    selftest_abort();
#endif

    hal_cpu_halt();
}
