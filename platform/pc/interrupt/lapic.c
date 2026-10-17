/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lapic.h"

#include "hal/mmu.h"

#include "platform/pc/vmap/vmap.h"

#include <stddef.h>

static volatile uint32_t *registers;

int lapic_init(uint32_t base)
{
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
