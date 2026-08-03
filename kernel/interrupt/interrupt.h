/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_INTERRUPT_INTERRUPT_H
#define KERNEL_INTERRUPT_INTERRUPT_H

#include "interrupt_frame.h"

void interrupt_handler(struct interrupt_frame *frame);

#endif