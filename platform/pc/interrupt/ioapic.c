/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ioapic.h"

#include "hal/acpi.h"
#include "hal/mmu.h"

#include "platform/pc/vmap/vmap.h"

#include "arch/x86/cpu/cpu.h"

#include <stddef.h>

/*
 * Registers are reached through two memory-mapped words: write the register number to
 * IOREGSEL (offset 0), then read or write IOWIN (offset 0x10). Register 1 is the version
 * (bits 23:16 = number of the last redirection entry); entry n is registers 0x10 + 2n (low
 * word) and 0x11 + 2n (high word). (82093AA datasheet 3.0.)
 */
#define IOREGSEL 0x00
#define IOWIN 0x10
#define REG_VERSION 0x01
#define REG_REDIRECTION 0x10

/* Redirection entry, low word. */
#define REDIRECTION_POLARITY_LOW (1u << 13)
#define REDIRECTION_LEVEL (1u << 15)
#define REDIRECTION_MASKED (1u << 16)

typedef struct
{
    volatile uint32_t *registers;
    uint32_t gsi_base;
    uint32_t inputs;
} ioapic_t;

static ioapic_t chips[HAL_ACPI_MAX_IOAPICS];
static unsigned int chip_count;

static uint32_t chip_read(const ioapic_t *chip, uint32_t reg)
{
    chip->registers[IOREGSEL / 4] = reg;

    return chip->registers[IOWIN / 4];
}

static void chip_write(const ioapic_t *chip, uint32_t reg, uint32_t value)
{
    chip->registers[IOREGSEL / 4] = reg;
    chip->registers[IOWIN / 4] = value;
}

static const ioapic_t *chip_for(uint32_t gsi)
{
    for (unsigned int i = 0; i < chip_count; i++)
    {
        if (gsi >= chips[i].gsi_base && gsi < chips[i].gsi_base + chips[i].inputs)
            return &chips[i];
    }

    return NULL;
}

int ioapic_init(void)
{
    const hal_acpi_info_t *acpi = hal_acpi_info();

    if (acpi == NULL)
        return 0;

    for (uint32_t i = 0; i < acpi->ioapic_count && chip_count < HAL_ACPI_MAX_IOAPICS; i++)
    {
        ioapic_t *chip = &chips[chip_count];

        chip->registers = vmap(acpi->ioapic_base[i], 4096, HAL_MMU_WRITABLE | HAL_MMU_UNCACHED);

        if (chip->registers == NULL)
            continue;

        chip->gsi_base = acpi->ioapic_gsi_base[i];
        chip->inputs = ((chip_read(chip, REG_VERSION) >> 16) & 0xFF) + 1;

        for (uint32_t pin = 0; pin < chip->inputs; pin++)
            chip_write(chip, REG_REDIRECTION + 2 * pin, REDIRECTION_MASKED);

        chip_count++;
    }

    return (int)chip_count;
}

int ioapic_route(uint32_t gsi, uint8_t vector, uint32_t destination, uint32_t flags)
{
    const ioapic_t *chip = chip_for(gsi);

    if (chip == NULL)
        return -1;

    uint32_t pin = gsi - chip->gsi_base;
    uint32_t low = vector | REDIRECTION_MASKED;
    uint32_t irq_flags = cpu_irq_save(); /* the index/data register pair is not re-entrant */

    if (flags & IOAPIC_POLARITY_LOW)
        low |= REDIRECTION_POLARITY_LOW;

    if (flags & IOAPIC_TRIGGER_LEVEL)
        low |= REDIRECTION_LEVEL;

    /* High word first: the destination must be right before the entry can fire. */
    chip_write(chip, REG_REDIRECTION + 2 * pin + 1, destination << 24);
    chip_write(chip, REG_REDIRECTION + 2 * pin, low);
    cpu_irq_restore(irq_flags);

    return 0;
}

static int set_mask(uint32_t gsi, int masked)
{
    const ioapic_t *chip = chip_for(gsi);

    if (chip == NULL)
        return -1;

    uint32_t reg = REG_REDIRECTION + 2 * (gsi - chip->gsi_base);
    uint32_t irq_flags = cpu_irq_save();
    uint32_t low = chip_read(chip, reg);

    low = masked ? (low | REDIRECTION_MASKED) : (low & ~REDIRECTION_MASKED);
    chip_write(chip, reg, low);
    cpu_irq_restore(irq_flags);

    return 0;
}

int ioapic_unmask(uint32_t gsi)
{
    return set_mask(gsi, 0);
}

int ioapic_mask(uint32_t gsi)
{
    return set_mask(gsi, 1);
}

uint32_t ioapic_input_count(void)
{
    uint32_t total = 0;

    for (unsigned int i = 0; i < chip_count; i++)
        total += chips[i].inputs;

    return total;
}
