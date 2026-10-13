/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pic.h"

#include "arch/x86/interrupt/irq_stub.h"
#include "arch/x86/io/io.h"

#define PIC1_COMMAND 0x20 // Master PIC command port
#define PIC1_DATA 0x21    // Master PIC data port

#define PIC2_COMMAND 0xA0 // Slave PIC command port
#define PIC2_DATA 0xA1    // Slave PIC data port

#define PIC_EOI 0x20

/* Master input the slave is wired to (OSDev Wiki "8259 PIC"). */
#define PIC_CASCADE_LINE 2

void pic_init(void)
{
    // ICW1
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);

    // ICW2
    outb(PIC1_DATA, IRQ_STUB_VECTOR_BASE);
    outb(PIC2_DATA, IRQ_STUB_VECTOR_BASE + 8);

    // ICW3
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    // ICW4
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    // mask every line; drivers unmask their own
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

/*
 * Reading or writing the data port outside initialisation accesses the
 * IMR (OCW1); a set bit masks that line (Intel 8259A datasheet).
 */
static void pic_clear_mask_bit(uint16_t data_port, unsigned int bit)
{
    outb(data_port, inb(data_port) & ~(1u << bit));
}

void pic_unmask(unsigned int line)
{
    if (line < 8)
    {
        pic_clear_mask_bit(PIC1_DATA, line);
        return;
    }

    pic_clear_mask_bit(PIC2_DATA, line - 8);
    pic_clear_mask_bit(PIC1_DATA, PIC_CASCADE_LINE);
}

void pic_send_eoi(unsigned char irq)
{
    if (irq >= 8)
    {
        outb(PIC2_COMMAND, PIC_EOI);
    }

    outb(PIC1_COMMAND, PIC_EOI);
}