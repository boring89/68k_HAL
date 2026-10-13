/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_INTERRUPT_PIC_H
#define PLATFORM_PC_INTERRUPT_PIC_H

/* Master (lines 0-7) and slave (lines 8-15) 8259A. */
#define PIC_LINE_COUNT 16

/* Remap the PICs to IRQ_STUB_VECTOR_BASE and mask every line. */
void pic_init(void);

/* Unmask line (< PIC_LINE_COUNT); lines 8-15 also unmask the cascade. */
void pic_unmask(unsigned int line);

void pic_send_eoi(unsigned char irq);

#endif