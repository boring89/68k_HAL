/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gdt.h"

/* Defined in gdt.asm. */
extern uint64_t gdt_start[];

/*
 * 32-bit TSS (Intel SDM Vol.3A 7.2.1, Figure 7-2). Only ss0:esp0 is
 * used: there is no hardware task switching and no I/O permission
 * bitmap (iomap_base beyond the limit means every port access from
 * ring 3 faults).
 */
struct tss
{
    uint32_t link;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t unused[22];
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

static struct tss tss;

/* Descriptor with a 20-bit limit (bytes or 4 KiB pages, see flags 0x8). */
static uint64_t make_descriptor(uint32_t base, uint32_t limit, uint8_t access,
                                uint8_t flags)
{
    uint64_t d = limit & 0xFFFF;

    d |= (uint64_t)(base & 0xFFFFFF) << 16;
    d |= (uint64_t)access << 40;
    d |= (uint64_t)((limit >> 16) & 0xF) << 48;
    d |= (uint64_t)(flags & 0xF) << 52;
    d |= (uint64_t)(base >> 24) << 56;

    return d;
}

void gdt_init_tss(void)
{
    tss.ss0 = GDT_KERNEL_DATA;
    tss.iomap_base = sizeof(tss);

    /* 0x89: present, DPL 0, type 9 = available 32-bit TSS (SDM Vol.3A Table 3-2). */
    gdt_start[GDT_TSS >> 3] =
        make_descriptor((uint32_t)(uintptr_t)&tss, sizeof(tss) - 1, 0x89, 0x0);

    asm volatile("ltr %0" : : "r"((uint16_t)GDT_TSS));
}

void gdt_set_kernel_stack(uint32_t esp0)
{
    tss.esp0 = esp0;
}

void gdt_set_tls(uint32_t base)
{
    /* 0xF2: present, DPL 3, writable data; flags 0xC: 4 KiB granularity, 32-bit. */
    gdt_start[GDT_TLS >> 3] = make_descriptor(base, 0xFFFFF, 0xF2, 0xC);
}
