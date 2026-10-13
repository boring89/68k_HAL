/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_INTERRUPT_EXCEPTION_H
#define KERNEL_INTERRUPT_EXCEPTION_H

#include "interrupt_frame.h"

/* Vector 14 (Intel SDM Vol.3A 6.15, Table 6-1). */
#define EXCEPTION_PAGE_FAULT 14

/* Page fault error code bits (Intel SDM Vol.3A 4.7, Figure 4-12). */
#define EXCEPTION_PF_PRESENT  0x01 /* 0: page not present, 1: protection violation */
#define EXCEPTION_PF_WRITE    0x02 /* 0: read, 1: write */
#define EXCEPTION_PF_USER     0x04 /* 0: ring 0, 1: ring 3 */
#define EXCEPTION_PF_RESERVED 0x08 /* reserved bit set in a paging entry */
#define EXCEPTION_PF_FETCH    0x10 /* instruction fetch (needs NX or SMEP) */

/* Called by isr_common_stub (isr.asm) for CPU exceptions 0-31. */
void exception_handler(struct interrupt_frame *frame);

#ifdef KERNEL_SELFTEST
/*
 * Test-only hook called before the default handler. Returning 0 marks
 * the exception as handled and resumes at frame->eip; any other value
 * falls through to panic. Pass 0 to remove the hook.
 */
typedef int (*exception_hook_t)(struct interrupt_frame *frame);

void exception_set_test_hook(exception_hook_t hook);
#endif

#endif
