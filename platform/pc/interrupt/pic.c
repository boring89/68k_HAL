/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pic.h"

#include "arch/x86/cpu/cpu.h"
#include "arch/x86/interrupt/irq_stub.h"
#include "arch/x86/io/io.h"

#define PIC1_COMMAND 0x20 // Master PIC command port
#define PIC1_DATA 0x21    // Master PIC data port

#define PIC2_COMMAND 0xA0 // Slave PIC command port
#define PIC2_DATA 0xA1    // Slave PIC data port

#define PIC_EOI 0x20

/* OCW3: the next read of the command port returns the ISR (8259A datasheet). */
#define PIC_OCW3_READ_ISR 0x0B

/* The lowest-priority line of each chip, where spurious IRQs show up. */
#define PIC_MASTER_SPURIOUS 7
#define PIC_SLAVE_SPURIOUS 15

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
/*
 * Read-modify-write of the IMR: an IRQ handler that changes the mask in
 * between would have its change overwritten, so keep interrupts off.
 */
static void pic_clear_mask_bit(uint16_t data_port, unsigned int bit)
{
    uint32_t flags = cpu_irq_save();

    outb(data_port, inb(data_port) & ~(1u << bit));

    cpu_irq_restore(flags);
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

void pic_mask(unsigned int line)
{
    uint16_t port = line < 8 ? PIC1_DATA : PIC2_DATA;
    uint32_t flags = cpu_irq_save();

    outb(port, inb(port) | (1u << (line & 7)));

    cpu_irq_restore(flags);
}

void pic_send_eoi(unsigned char irq)
{
    if (irq >= 8)
    {
        outb(PIC2_COMMAND, PIC_EOI);
    }

    outb(PIC1_COMMAND, PIC_EOI);
}

static uint32_t spurious_count;

/* In-service register: bit n set while line n is being handled. */
static uint8_t pic_read_isr(uint16_t command_port)
{
    outb(command_port, PIC_OCW3_READ_ISR);

    return inb(command_port);
}

/*
 * A request that goes away before the CPU acknowledges it is still
 * delivered, as IRQ 7 (master) or IRQ 15 (slave), but the line's ISR bit
 * stays clear (OSDev Wiki "8259 PIC", Spurious IRQs).
 */
int pic_handle_spurious(unsigned int irq)
{
    if (irq == PIC_MASTER_SPURIOUS)
    {
        if (pic_read_isr(PIC1_COMMAND) & (1u << 7))
            return 0;
    }
    else if (irq == PIC_SLAVE_SPURIOUS)
    {
        if (pic_read_isr(PIC2_COMMAND) & (1u << 7))
            return 0;

        /* The master did see a real request on its cascade line. */
        outb(PIC1_COMMAND, PIC_EOI);
    }
    else
    {
        return 0;
    }

    spurious_count++;

    return 1;
}

uint32_t pic_get_spurious_count(void)
{
    return spurious_count;
}
