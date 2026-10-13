/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_TEST_SELFTEST_H
#define KERNEL_TEST_SELFTEST_H

/*
 * Runs the in-kernel self tests, then exits QEMU through the
 * isa-debug-exit device. Must be called after interrupts are enabled.
 * Never returns.
 */
void selftest_run(void);

/* Exits QEMU with the FAIL status. Used by panic() in selftest builds. */
void selftest_abort(void) __attribute__((noreturn));

#endif
