/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_ACPI_H
#define HAL_ACPI_H

#include <stdint.h>

/*
 * What the firmware's ACPI tables say about the machine. Implemented
 * by platforms that have ACPI (platform/pc/acpi/hal_acpi.c).
 */

#define HAL_ACPI_MAX_CPUS 32
#define HAL_ACPI_MAX_IOAPICS 8
#define HAL_ACPI_MAX_OVERRIDES 16

/*
 * "Interrupt source override" (MADT entry type 2): ISA interrupt line `source` is wired to
 * global system interrupt (I/O APIC input) `gsi`, with the polarity and trigger mode in flags
 * (bits 1:0 polarity: 1 = active high, 3 = active low; bits 3:2 trigger: 1 = edge, 3 = level;
 * 0 = as the bus does, which for ISA is active high and edge).
 */
typedef struct
{
    uint8_t source;
    uint32_t gsi;
    uint16_t flags;
} hal_acpi_irq_override_t;

typedef struct
{
    char oem_id[7];          /* from the RSDT header, e.g. "BOCHS" */
    uint8_t revision;        /* ACPI revision of the RSDP: 0 = 1.0, 2 = 2.0+ */
    uint32_t cpu_count;      /* enabled processors listed in the MADT */
    uint32_t apic_ids[HAL_ACPI_MAX_CPUS]; /* local APIC ID of each, in order */
    uint32_t lapic_base;     /* physical address of the local APIC registers */
    uint32_t ioapic_count;
    uint32_t ioapic_base[HAL_ACPI_MAX_IOAPICS];
    uint32_t ioapic_id[HAL_ACPI_MAX_IOAPICS];
    uint32_t ioapic_gsi_base[HAL_ACPI_MAX_IOAPICS]; /* first interrupt input of each */
    uint32_t irq_override_count;
    hal_acpi_irq_override_t irq_override[HAL_ACPI_MAX_OVERRIDES];
    int dual_8259;           /* the machine also has the legacy 8259 PICs */
} hal_acpi_info_t;

/* The parsed tables; NULL if the machine has no usable ACPI. */
const hal_acpi_info_t *hal_acpi_info(void);

#endif
