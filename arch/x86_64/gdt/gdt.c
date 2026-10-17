/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gdt.h"

/*
 * 64-bit TSS (Intel SDM Vol.3A 7.7, Figure 7-11). There is no hardware task switching: it holds
 * the stack pointers the CPU loads on a privilege change (rsp0) and the interrupt stack table.
 * No I/O permission bitmap (iomap_base beyond the limit: every port access from ring 3 faults).
 */
struct tss
{
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

_Static_assert(sizeof(struct tss) == 104, "the 64-bit TSS is 104 bytes");

static struct tss tss;

/* Stacks for the double fault and the NMI: they run even when the kernel stack is unusable. */
#define IST_STACK_SIZE 8192
static uint8_t ist_double_fault_stack[IST_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t ist_nmi_stack[IST_STACK_SIZE] __attribute__((aligned(16)));

/*
 * The table: 0 null, 0x08 kernel code (L = 1), 0x10 kernel data, 0x18 user code (32-bit),
 * 0x20 user data, 0x28 unused, 0x30 TLS, 0x38 TSS (two slots). Must be writable: the CPU sets
 * the Accessed bit of a descriptor when it loads a segment register (SDM Vol.3A 3.4.5.1).
 */
#define GDT_SLOTS 9

static uint64_t gdt[GDT_SLOTS] __attribute__((aligned(16))) = {
    0,
    0x00209A0000000000ull, /* code: P, DPL 0, execute/read, L */
    0x0000920000000000ull, /* data: P, DPL 0, read/write */
    0x00CFFA000000FFFFull, /* user code: P, DPL 3, execute/read, 32-bit, 4 KiB granularity */
    0x00CFF2000000FFFFull, /* user data: P, DPL 3, read/write */
    0,
    0,                     /* TLS: gdt_set_tls */
    0,                     /* TSS: gdt_init_tss */
    0,
};

struct gdt_register
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void gdt_init(void)
{
    struct gdt_register gdtr = {sizeof(gdt) - 1, (uint64_t)(uintptr_t)gdt};

    /*
     * lgdt, then reload CS with a far return (there is no far jump with a register operand in
     * 64-bit mode) and the data segments.
     */
    asm volatile(
        "lgdt %0\n\t"
        "pushq %1\n\t"
        "lea 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "mov %2, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "xor %%eax, %%eax\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs"
        :
        : "m"(gdtr), "i"((uint64_t)GDT_KERNEL_CODE), "i"((uint16_t)GDT_KERNEL_DATA)
        : "rax", "memory");
}

void gdt_init_tss(void)
{
    uint64_t base = (uint64_t)(uintptr_t)&tss;
    uint64_t limit = sizeof(tss) - 1;

    tss.iomap_base = sizeof(tss);
    tss.ist[GDT_IST_DOUBLE_FAULT - 1] = (uint64_t)(uintptr_t)(ist_double_fault_stack + IST_STACK_SIZE);
    tss.ist[GDT_IST_NMI - 1] = (uint64_t)(uintptr_t)(ist_nmi_stack + IST_STACK_SIZE);

    /* 0x89: present, DPL 0, type 9 = available 64-bit TSS (SDM Vol.3A Table 3-2, Figure 7-4). */
    uint64_t low = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89ull << 40) |
                   (((limit >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);

    gdt[GDT_TSS >> 3] = low;
    gdt[(GDT_TSS >> 3) + 1] = base >> 32;

    asm volatile("ltr %0" : : "r"((uint16_t)GDT_TSS));
}

void gdt_set_kernel_stack(uintptr_t rsp0)
{
    tss.rsp[0] = rsp0;
}

uintptr_t gdt_get_kernel_stack(void)
{
    return (uintptr_t)tss.rsp[0];
}

void gdt_set_tls(uint32_t base)
{
    uint64_t limit = 0xFFFFF;

    /* 0xF2: present, DPL 3, writable data; flags 0xC: 4 KiB granularity, 32-bit. */
    gdt[GDT_TLS >> 3] = (limit & 0xFFFF) | ((uint64_t)(base & 0xFFFFFF) << 16) | (0xF2ull << 40) |
                        ((limit >> 16) << 48) | (0xCull << 52) | ((uint64_t)(base >> 24) << 56);
}
