/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_CPU_H
#define HAL_CPU_H

#include <stdint.h>


/*
 * Control of the current CPU. Implemented once per architecture
 * (arch/<arch>/cpu/hal_cpu.c).
 */

/*
 * Let external interrupts (timer, keyboard, ...) reach the CPU.
 * Also a compiler barrier: memory accesses are not moved across it.
 */
void hal_cpu_enable_interrupts(void);

/*
 * Stop external interrupts from reaching the CPU until
 * hal_cpu_enable_interrupts(). CPU exceptions still happen.
 * Also a compiler barrier.
 */
void hal_cpu_disable_interrupts(void);

/*
 * Critical sections: hal_cpu_irq_save disables interrupts and returns whether they were on;
 * hal_cpu_irq_restore puts that state back (so the section may nest, or be entered with
 * interrupts already off).
 */
uint32_t hal_cpu_irq_save(void);
void hal_cpu_irq_restore(uint32_t state);

/*
 * Sleep until the next interrupt arrives and has been handled.
 * Interrupts must be enabled, or the CPU never wakes up.
 */
void hal_cpu_idle(void);

/* Disable interrupts and stop the CPU for good. */
void hal_cpu_halt(void) __attribute__((noreturn));

/*
 * What the CPU reports about itself (CPUID). Feature bits are the
 * HAL_CPU_FEATURE_* masks.
 */

#define HAL_CPU_FEATURE_FPU 0x00000001u      /* x87 floating point unit */
#define HAL_CPU_FEATURE_TSC 0x00000002u      /* time stamp counter */
#define HAL_CPU_FEATURE_MSR 0x00000004u      /* model specific registers */
#define HAL_CPU_FEATURE_PAE 0x00000008u      /* physical address extension */
#define HAL_CPU_FEATURE_APIC 0x00000010u     /* local APIC on chip */
#define HAL_CPU_FEATURE_CMOV 0x00000020u     /* conditional move */
#define HAL_CPU_FEATURE_PGE 0x00000040u      /* global pages */
#define HAL_CPU_FEATURE_FXSR 0x00000080u     /* FXSAVE / FXRSTOR */
#define HAL_CPU_FEATURE_SSE 0x00000100u
#define HAL_CPU_FEATURE_SSE2 0x00000200u
#define HAL_CPU_FEATURE_SSE3 0x00000400u
#define HAL_CPU_FEATURE_SSSE3 0x00000800u
#define HAL_CPU_FEATURE_SSE41 0x00001000u
#define HAL_CPU_FEATURE_SSE42 0x00002000u
#define HAL_CPU_FEATURE_X2APIC 0x00004000u
#define HAL_CPU_FEATURE_POPCNT 0x00008000u
#define HAL_CPU_FEATURE_XSAVE 0x00010000u
#define HAL_CPU_FEATURE_AVX 0x00020000u
#define HAL_CPU_FEATURE_NX 0x00040000u       /* no-execute page bit */
#define HAL_CPU_FEATURE_INVARIANT_TSC 0x00080000u /* TSC rate never changes */
#define HAL_CPU_FEATURE_HYPERVISOR 0x00100000u    /* running in a virtual machine */
#define HAL_CPU_FEATURE_PAT 0x00200000u           /* page attribute table (write combining) */

typedef struct
{
    char vendor[13]; /* "GenuineIntel", "AuthenticAMD", ... */
    char brand[49];  /* marketing name; empty if the CPU has none */
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t features; /* HAL_CPU_FEATURE_* */
} hal_cpu_info_t;

/*
 * Identify the CPU and switch on its floating point / SSE units so
 * that user programs can use them. Call once, early in hal_init.
 */
void hal_cpu_init(void);

/*
 * Number of processors that are running, the boot CPU included (1 if the
 * machine has one or the others could not be started). The extra ones
 * only report and halt; the kernel runs on the boot CPU. Implemented by
 * the platform (platform/<platform>/smp/smp.c).
 */
unsigned int hal_cpu_online_count(void);

/* The result of hal_cpu_init(). */
const hal_cpu_info_t *hal_cpu_info(void);

#endif
