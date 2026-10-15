/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_TEST_SELFTEST_H
#define KERNEL_TEST_SELFTEST_H

#include "kernel/printf/printf.h"

/*
 * Runs the in-kernel self tests, then exits QEMU through the
 * isa-debug-exit device. Must be called after interrupts are enabled.
 * Never returns.
 */
void selftest_run(void);

/* Exits QEMU with the FAIL status. Used by panic() in selftest builds. */
void selftest_abort(void) __attribute__((noreturn));

/*
 * For test suites. A test is a void function that checks with
 * SELFTEST_ASSERT; a suite runs its tests with SELFTEST_RUN.
 */
void selftest_report_failure(const char *file, int line, const char *cond);
int selftest_failure_count(void);

#define SELFTEST_ASSERT(cond)                                   \
    do                                                          \
    {                                                           \
        if (!(cond))                                            \
            selftest_report_failure(__FILE__, __LINE__, #cond); \
    } while (0)

#define SELFTEST_RUN(fn)                                        \
    do                                                          \
    {                                                           \
        int before = selftest_failure_count();                  \
        fn();                                                   \
        kernel_printf("[TEST] %s %s\n", #fn,                    \
                      selftest_failure_count() == before        \
                          ? "PASS" : "FAIL");                   \
    } while (0)

/*
 * Provided below the kernel: the architecture's tests
 * (arch/<arch>/test/), the platform's tests and the way to end the run
 * (platform/<platform>/test/). selftest_run calls them after the kernel
 * tests.
 */
void selftest_arch_run(void);
void selftest_platform_run(void);
void selftest_platform_exit(int passed) __attribute__((noreturn));

#endif
