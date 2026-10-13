/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_INTERRUPT_EXCEPTION_H
#define KERNEL_INTERRUPT_EXCEPTION_H

#include "interrupt_frame.h"

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
