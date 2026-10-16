/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_CPU_CPU_H
#define ARCH_X86_CPU_CPU_H

#include <stdint.h>

/* CR0 bits (Intel SDM Vol.3A 2.5, Figure 2-7). */
#define CPU_CR0_PE 0x00000001u /* bit 0: protected mode enable */
#define CPU_CR0_WP 0x00010000u /* bit 16: write protect, ring 0 honours R/W */
#define CPU_CR0_MP 0x00000002u /* bit 1: monitor coprocessor (WAIT honours TS) */
#define CPU_CR0_EM 0x00000004u /* bit 2: emulate FPU; x87/SSE raise #NM when set */
#define CPU_CR0_TS 0x00000008u /* bit 3: task switched */
#define CPU_CR0_NE 0x00000020u /* bit 5: x87 errors raise #MF instead of IRQ 13 */
#define CPU_CR0_PG 0x80000000u /* bit 31: paging */

/* CR4 bits (Intel SDM Vol.3A 2.5, Figure 2-7). */
#define CPU_CR4_OSFXSR 0x00000200u     /* bit 9: OS uses FXSAVE/FXRSTOR, SSE allowed */
#define CPU_CR4_OSXMMEXCPT 0x00000400u /* bit 10: unmasked SSE faults raise #XM */

/* EFLAGS bits (Intel SDM Vol.1 3.4.3, Figure 3-8). */
#define CPU_EFLAGS_IF 0x00000200u /* bit 9: maskable interrupts enabled */

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

static inline uint32_t cpu_read_cr4(void)
{
    uint32_t value;

    asm volatile(
        "mov %%cr4, %0"
        : "=r"(value)
        :
        : "memory"
    );

    return value;
}

static inline void cpu_write_cr4(uint32_t value)
{
    asm volatile(
        "mov %0, %%cr4"
        :
        : "r"(value)
        : "memory"
    );
}

/* Time stamp counter (Intel SDM Vol.2B "RDTSC"). Needs HAL_CPU_FEATURE_TSC. */
static inline uint64_t cpu_rdtsc(void)
{
    uint32_t lo;
    uint32_t hi;

    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));

    return ((uint64_t)hi << 32) | lo;
}

/* Faulting linear address of the last page fault (Intel SDM Vol.3A 2.5). */
static inline uint32_t cpu_read_cr2(void)
{
    uint32_t value;

    asm volatile(
        "mov %%cr2, %0"
        : "=r"(value)
        :
        : "memory"
    );

    return value;
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

/*
 * Disable maskable interrupts and return the previous EFLAGS, for a short
 * critical section. cpu_irq_restore puts IF back the way it was, so the
 * pair also works when interrupts were already off.
 */
static inline uint32_t cpu_irq_save(void)
{
    uint32_t flags;

    asm volatile(
        "pushfl\n\t"
        "popl %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );

    return flags;
}

static inline void cpu_irq_restore(uint32_t flags)
{
    if (flags & CPU_EFLAGS_IF)
        asm volatile("sti" : : : "memory");
}

#endif
