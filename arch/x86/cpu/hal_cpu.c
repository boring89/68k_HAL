/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/cpu.h"

#include "cpu.h"
#include "cpuid.h"

/*
 * STI / CLI set and clear EFLAGS.IF (bit 9), which masks maskable
 * external interrupts (Intel SDM Vol.2B "STI", Vol.2A "CLI"; Vol.3A 6.8.1).
 * The "memory" clobber makes each one a compiler barrier.
 */

void hal_cpu_enable_interrupts(void)
{
    asm volatile("sti" : : : "memory");
}

void hal_cpu_disable_interrupts(void)
{
    asm volatile("cli" : : : "memory");
}

uint32_t hal_cpu_irq_save(void)
{
    return cpu_irq_save();
}

void hal_cpu_irq_restore(uint32_t state)
{
    cpu_irq_restore(state);
}

/*
 * HLT stops the CPU until an interrupt, NMI or reset arrives; the
 * handler runs, then execution continues after HLT (Intel SDM Vol.2A
 * "HLT"). The "memory" clobber makes callers re-read variables written
 * by that handler.
 */

void hal_cpu_idle(void)
{
    asm volatile("hlt" : : : "memory");
}

void hal_cpu_halt(void)
{
    hal_cpu_disable_interrupts();

    /* An NMI still wakes HLT even with IF clear, so halt again. */
    for (;;)
        asm volatile("hlt");
}

static hal_cpu_info_t cpu_info;

/*
 * Enable the x87 FPU and SSE (Intel SDM Vol.3A 13.1.2 "Initializing
 * the FPU" and 13.1.3 "Initializing SSE/SSE2/SSE3 Extensions"):
 *   CR0.EM = 0  the FPU is real, do not trap x87/SSE instructions
 *   CR0.MP = 1  WAIT/FWAIT honours CR0.TS
 *   CR0.NE = 1  x87 errors raise #MF instead of the old IRQ 13 wire
 *   CR4.OSFXSR, OSXMMEXCPT = 1 (only if the CPU has SSE)
 * then FNINIT puts the x87 into its power-on state.
 *
 * The kernel itself does not use the FPU; the registers are not saved
 * on interrupts. User programs run one at a time (no task switching).
 */
void hal_cpu_init(void)
{
    cpuid_raw_t raw;

    if (cpuid_read(&raw) == 0)
        cpuid_decode(&raw, &cpu_info);

    if (!(cpu_info.features & HAL_CPU_FEATURE_FPU))
        return;

    uint32_t cr0 = cpu_read_cr0();

    cr0 &= ~(CPU_CR0_EM | CPU_CR0_TS);
    cr0 |= CPU_CR0_MP | CPU_CR0_NE;
    cpu_write_cr0(cr0);

    asm volatile("fninit");

    if ((cpu_info.features & HAL_CPU_FEATURE_FXSR) &&
        (cpu_info.features & HAL_CPU_FEATURE_SSE))
        cpu_write_cr4(cpu_read_cr4() | CPU_CR4_OSFXSR | CPU_CR4_OSXMMEXCPT);
}

const hal_cpu_info_t *hal_cpu_info(void)
{
    return &cpu_info;
}
