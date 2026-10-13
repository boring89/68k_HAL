/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_INTERRUPT_ISR_H
#define KERNEL_INTERRUPT_ISR_H

#include <stdint.h>

#define ISR_EXCEPTION_COUNT 32

/* Addresses of the exception stubs for vectors 0-31 (isr.asm). */
extern const uint32_t isr_stub_table[ISR_EXCEPTION_COUNT];

#endif
