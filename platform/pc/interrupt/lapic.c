/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lapic.h"

#include "hal/mmu.h"

#include "hal/cpu.h"

#include "platform/pc/vmap/vmap.h"

#include "arch/x86/cpu/cpu.h"

#include <stddef.h>

static volatile uint32_t *registers;

/*
 * IA32_APIC_BASE (SDM Vol.3A 10.4.4, 10.12): bit 11 = APIC enabled, bit 10 = x2APIC mode. Servers'
 * firmware often leaves the APIC in x2APIC mode, where the memory-mapped registers do not exist
 * (writes go nowhere, so no end of interrupt would ever arrive). Go back to xAPIC mode: x2APIC
 * cannot switch directly, the APIC is disabled first (the allowed transition, 10.12.5).
 */
#define MSR_APIC_BASE 0x1B
#define APIC_BASE_X2APIC (1ull << 10)
#define APIC_BASE_ENABLE (1ull << 11)

static void leave_x2apic_mode(void)
{
    if (!(hal_cpu_info()->features & HAL_CPU_FEATURE_MSR))
        return;

    uint64_t base = cpu_rdmsr(MSR_APIC_BASE);

    if (!(base & APIC_BASE_X2APIC))
        return;

    cpu_wrmsr(MSR_APIC_BASE, base & ~(APIC_BASE_X2APIC | APIC_BASE_ENABLE));
    cpu_wrmsr(MSR_APIC_BASE, (base & ~APIC_BASE_X2APIC) | APIC_BASE_ENABLE);
}

int lapic_init(uint32_t base)
{
    leave_x2apic_mode();

    if (registers == NULL)
        registers = vmap(base, 4096, HAL_MMU_WRITABLE | HAL_MMU_UNCACHED);

    if (registers == NULL)
        return -1;

    /* Task priority 0: no interrupt is held back (SDM Vol.3A 10.8.3.1). */
    lapic_write(LAPIC_TPR, 0);

    /* Software enable (SVR bit 8) and the spurious interrupt vector (SDM Vol.3A 10.9). */
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | LAPIC_SPURIOUS_VECTOR);

    return 0;
}

int lapic_ready(void)
{
    return registers != NULL;
}

uint32_t lapic_read(uint32_t reg)
{
    return registers[reg / 4];
}

void lapic_write(uint32_t reg, uint32_t value)
{
    registers[reg / 4] = value;
}

uint32_t lapic_id(void)
{
    return lapic_read(LAPIC_ID) >> 24;
}

void lapic_eoi(void)
{
    lapic_write(LAPIC_EOI, 0);
}
