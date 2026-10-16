/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "idt.h"
#include "isr.h"
#include "irq_stub.h"

#include "arch/x86/user/user.h"

struct idt_entry
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtr;

static void idt_set_gate(uint8_t num, uint32_t handler)
{
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = 0x08;
    idt[num].zero = 0;
    idt[num].type_attr = 0x8E;
    idt[num].offset_high = (handler >> 16) & 0xFFFF;
}

static void lidt(struct idt_ptr *ptr)
{
    asm volatile(
        "lidt (%0)"
        :
        : "r"(ptr));
}

void idt_init(void)
{
    for (int i = 0; i < 256; i++)
    {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].type_attr = 0;
        idt[i].offset_high = 0;
    }

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint32_t)&idt;

    for (int i = 0; i < ISR_EXCEPTION_COUNT; i++)
        idt_set_gate(i, isr_stub_table[i]);

    for (int i = 0; i < IRQ_STUB_COUNT; i++)
        idt_set_gate(IRQ_STUB_VECTOR_BASE + i, irq_stub_table[i]);

    /*
     * int 0x80: system calls. Type 0xEE = present, DPL 3 (ring 3 may
     * execute INT 0x80, Intel SDM Vol.3A 6.12.1.1), 32-bit interrupt gate.
     */
    idt_set_gate(0x80, (uint32_t)(uintptr_t)user_syscall_stub);
    idt[0x80].type_attr = 0xEE;

    lidt(&idtr);
}