/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_CPU_CPU_H
#define ARCH_X86_CPU_CPU_H

#include <stdint.h>

/* CR0 bits (Intel SDM Vol.3A 2.5, Figure 2-7). */
#define CPU_CR0_PE 0x00000001u /* bit 0: protected mode enable */
#define CPU_CR0_PG 0x80000000u /* bit 31: paging */

/*
 * The "memory" clobbers keep the compiler from moving page table writes
 * across these instructions: the CPU reads the tables from memory.
 */

static inline uint32_t cpu_read_cr0(void)
{
    uint32_t value;

    asm volatile(
        "mov %%cr0, %0"
        : "=r"(value)
        :
        : "memory"
    );

    return value;
}

static inline void cpu_write_cr0(uint32_t value)
{
    asm volatile(
        "mov %0, %%cr0"
        :
        : "r"(value)
        : "memory"
    );
}

static inline uint32_t cpu_read_cr3(void)
{
    uint32_t value;

    asm volatile(
        "mov %%cr3, %0"
        : "=r"(value)
        :
        : "memory"
    );

    return value;
}

/*
 * Load the physical address of the page directory. Also flushes all
 * non-global TLB entries (Intel SDM Vol.3A 4.10.4.1).
 */
static inline void cpu_write_cr3(uint32_t value)
{
    asm volatile(
        "mov %0, %%cr3"
        :
        : "r"(value)
        : "memory"
    );
}

/* Drop the TLB entry for the page containing virt (Intel SDM Vol.2A, INVLPG). */
static inline void cpu_invlpg(uintptr_t virt)
{
    asm volatile(
        "invlpg (%0)"
        :
        : "r"(virt)
        : "memory"
    );
}

#endif
