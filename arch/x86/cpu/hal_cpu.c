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
 * IA32_PAT (Intel SDM Vol.3A 11.12.2, Table 11-10 memory types: 0 UC, 1 WC, 4 WT, 5 WP, 6 WB, 7 UC-).
 * A page's PAT index is PAT * 4 + PCD * 2 + PWT. The power-on value is
 * 0x0007040600070406 (index 0 WB, 1 WT, 2 UC-, 3 UC, then the same again). We change index 1, a page with
 * PWT alone, from write-through to write combining (HAL_MMU_WRITE_COMBINING, hal/mmu.h); nothing else uses
 * PWT without PCD. Where the firmware's MTRRs say UC (the display memory of a PC), UC + WC = WC (SDM Vol.3A
 * 11.5.2.2, Table 11-7). Every processor that runs kernel code needs the same value; the others do not run any.
 */
#define PAT_VALUE 0x0007010600070106ull

static void pat_init(void)
{
    if (!(cpu_info.features & HAL_CPU_FEATURE_PAT) || !(cpu_info.features & HAL_CPU_FEATURE_MSR))
        return;

    /* A changed memory type needs the caches and the TLB flushed (SDM Vol.3A 11.12.4, 11.11.8); no page uses index 1 yet. */
    asm volatile("wbinvd" : : : "memory");
    cpu_wrmsr(CPU_MSR_PAT, PAT_VALUE);

    if (cpu_read_cr0() & CPU_CR0_PG)
        cpu_write_cr3(cpu_read_cr3());
}

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

    pat_init();

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
