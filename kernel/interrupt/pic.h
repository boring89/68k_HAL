/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_INTERRUPT_PIC_H
#define KERNEL_INTERRUPT_PIC_H

void pic_init(void);

void pic_send_eoi(unsigned char irq);

#endif