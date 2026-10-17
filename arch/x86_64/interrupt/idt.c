/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "arch/x86/interrupt/idt.h"
#include "arch/x86/interrupt/isr.h"
#include "arch/x86/interrupt/irq_stub.h"

#include "arch/x86_64/gdt/gdt.h"
#include "arch/x86_64/user/user.h"

/* 64-bit interrupt gate (Intel SDM Vol.3A 6.14.1, Figure 6-7): 16 bytes. */
struct idt_entry
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;       /* bits 2:0: interrupt stack table slot, 0 = none */
    uint8_t type_attr; /* P, DPL, type */
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct idt_entry) == 16, "a 64-bit gate is 16 bytes");

struct idt_ptr
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtr;

static void idt_set_gate(uint8_t num, uintptr_t handler, uint8_t ist, uint8_t type_attr)
{
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = GDT_KERNEL_CODE;
    idt[num].ist = ist;
    idt[num].type_attr = type_attr;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (uint32_t)(handler >> 32);
    idt[num].reserved = 0;
}

void idt_init(void)
{
    for (int i = 0; i < 256; i++)
        idt_set_gate((uint8_t)i, 0, 0, 0);

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)(uintptr_t)idt;

    /* 0x8E: present, DPL 0, 64-bit interrupt gate (IF cleared on entry). */
    for (int i = 0; i < ISR_EXCEPTION_COUNT; i++)
        idt_set_gate((uint8_t)i, isr_stub_table[i], 0, 0x8E);

    /* The double fault and the NMI get their own stacks (the TSS's IST). */
    idt_set_gate(8, isr_stub_table[8], GDT_IST_DOUBLE_FAULT, 0x8E);
    idt_set_gate(2, isr_stub_table[2], GDT_IST_NMI, 0x8E);

    for (int i = 0; i < IRQ_STUB_COUNT; i++)
        idt_set_gate((uint8_t)(IRQ_STUB_VECTOR_BASE + i), irq_stub_table[i], 0, 0x8E);

    idt_set_gate(0xFF, (uintptr_t)irq_spurious_stub, 0, 0x8E);

    /*
     * int 0x80: system calls. Type 0xEE = present, DPL 3 (ring 3 may execute INT 0x80, Intel SDM
     * Vol.3A 6.12.1.1), interrupt gate. A program in compatibility mode reaches it too.
     */
    idt_set_gate(0x80, (uintptr_t)user_syscall_stub, 0, 0xEE);

    asm volatile("lidt %0" : : "m"(idtr));
}
