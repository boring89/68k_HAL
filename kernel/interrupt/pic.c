/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pic.h"

#include "../../hal/io/io.h"

#define PIC1_COMMAND 0x20 // Master PIC command port
#define PIC1_DATA 0x21    // Master PIC data port

#define PIC2_COMMAND 0xA0 // Slave PIC command port
#define PIC2_DATA 0xA1    // Slave PIC data port

#define PIC_EOI 0x20

void pic_init(void)
{
    // save mask
    uint8_t master_mask = inb(PIC1_DATA);
    uint8_t slave_mask = inb(PIC2_DATA);

    // ICW1
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);

    // ICW2
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    // ICW3
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    // ICW4
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    // enable IRQ0 and IRQ1
    outb(PIC1_DATA, 0xFC);
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(unsigned char irq)
{
    if (irq >= 8)
    {
        outb(PIC2_COMMAND, PIC_EOI);
    }

    outb(PIC1_COMMAND, PIC_EOI);
}