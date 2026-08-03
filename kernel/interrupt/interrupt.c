/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "interrupt.h"
#include "../log/log.h"

#include "interrupt_frame.h"
#include "../printf/printf.h"

void interrupt_handler(struct interrupt_frame *frame)
{
    kernel_printf("Interrupt vector: %d\n", frame->int_no);
}