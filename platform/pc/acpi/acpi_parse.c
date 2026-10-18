/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "acpi_parse.h"

/*
 * MADT layout (ACPI Specification 6.x, Table 5.44 onward):
 *   header (36 bytes), u32 local APIC address, u32 flags (bit 0 =
 *   PCAT_COMPAT), then variable-length entries { u8 type, u8 length, ... }.
 *     type 0: processor local APIC   { u8 acpi id, u8 apic id, u32 flags (bit 0 = enabled) }
 *     type 1: I/O APIC               { u8 id, u8 reserved, u32 address, u32 gsi base }
 *     type 2: interrupt source override { u8 bus (0 = ISA), u8 source irq, u32 gsi, u16 flags }
 *     type 5: local APIC address override { u16 reserved, u64 address }
 */
#define MADT_ENTRIES_OFFSET 44

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

uint8_t acpi_checksum(const void *data, size_t length)
{
    const uint8_t *p = data;
    uint8_t sum = 0;

    for (size_t i = 0; i < length; i++)
        sum = (uint8_t)(sum + p[i]);

    return sum;
}

int acpi_rsdp_valid(const acpi_rsdp_t *rsdp)
{
    static const char signature[8] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};

    for (int i = 0; i < 8; i++)
    {
        if (rsdp->signature[i] != signature[i])
            return 0;
    }

    return acpi_checksum(rsdp, sizeof(*rsdp)) == 0;
}

int acpi_parse_madt(const uint8_t *madt, size_t length, hal_acpi_info_t *info)
{
    if (length < MADT_ENTRIES_OFFSET || read32(madt + 4) != length)
        return -1;

    if (madt[0] != 'A' || madt[1] != 'P' || madt[2] != 'I' || madt[3] != 'C')
        return -1;

    info->lapic_base = read32(madt + 36);
    info->dual_8259 = (read32(madt + 40) & 1) != 0;
    info->cpu_count = 0;
    info->ioapic_count = 0;
    info->irq_override_count = 0;

    size_t offset = MADT_ENTRIES_OFFSET;

    while (offset + 2 <= length)
    {
        uint8_t type = madt[offset];
        uint8_t size = madt[offset + 1];

        if (size < 2 || offset + size > length)
            return -1;

        const uint8_t *entry = madt + offset;

        if (type == 0 && size >= 8)
        {
            if ((read32(entry + 4) & 1) && info->cpu_count < HAL_ACPI_MAX_CPUS)
                info->apic_ids[info->cpu_count++] = entry[3];
        }
        else if (type == 1 && size >= 12)
        {
            if (info->ioapic_count < HAL_ACPI_MAX_IOAPICS)
            {
                info->ioapic_id[info->ioapic_count] = entry[2];
                info->ioapic_base[info->ioapic_count] = read32(entry + 4);
                info->ioapic_gsi_base[info->ioapic_count] = read32(entry + 8);
                info->ioapic_count++;
            }
        }
        else if (type == 2 && size >= 10)
        {
            if (entry[2] == 0 && info->irq_override_count < HAL_ACPI_MAX_OVERRIDES)
            {
                hal_acpi_irq_override_t *override = &info->irq_override[info->irq_override_count++];

                override->source = entry[3];
                override->gsi = read32(entry + 4);
                override->flags = (uint16_t)(entry[8] | entry[9] << 8);
            }
        }
        else if (type == 5 && size >= 12)
        {
            /* 64-bit address: only usable here if the high half is zero. */
            if (read32(entry + 8) == 0)
                info->lapic_base = read32(entry + 4);
        }

        offset += size;
    }

    return 0;
}

/*
 * FADT fields (ACPI Specification 6.x, Table 5.9): offset 36 FIRMWARE_CTRL, 40 DSDT, 48 SMI_CMD (u32 port),
 * 52 ACPI_ENABLE (u8), 64 PM1a_CNT_BLK, 68 PM1b_CNT_BLK, 112 flags (bit 10 RESET_REG_SUP), 116 RESET_REG
 * (generic address: u8 space id, width, offset, access size, u64 address), 128 RESET_VALUE.
 */
int acpi_parse_fadt(const uint8_t *fadt, size_t length, hal_acpi_info_t *info)
{
    if (length < 72 || fadt[0] != 'F' || fadt[1] != 'A' || fadt[2] != 'C' || fadt[3] != 'P')
        return -1;

    info->dsdt_address = read32(fadt + 40);
    info->smi_command_port = read32(fadt + 48);
    info->acpi_enable_value = fadt[52];
    info->pm1a_control_port = read32(fadt + 64);
    info->pm1b_control_port = read32(fadt + 68);

    /* PM1a/b_EVT_BLK at 56 and 60, PM1_EVT_LEN at 88 (ACPI Specification 6.x, table 5.9 "FADT Format"). */
    info->sci_irq = (uint16_t)(fadt[46] | fadt[47] << 8);
    info->pm1a_event_port = read32(fadt + 56);
    info->pm1b_event_port = read32(fadt + 60);
    info->pm1_event_length = length >= 89 ? fadt[88] : 0;

    info->reset_valid = 0;

    /* Old FADTs (ACPI 1.0, 116 bytes as in QEMU) end before the reset register. */
    if (length >= 129 && (read32(fadt + 112) & (1u << 10)) && read32(fadt + 120 + 4) == 0)
    {
        info->reset_space = fadt[116];
        info->reset_address = read32(fadt + 120);
        info->reset_value = fadt[128];
        info->reset_valid = info->reset_space <= 1;
    }

    return 0;
}

int acpi_find_s5(const uint8_t *dsdt, size_t length, uint8_t *type_a, uint8_t *type_b)
{
    /* The table header is 36 bytes; the AML starts after it. */
    for (size_t i = 36; i + 4 <= length; i++)
    {
        if (dsdt[i] != '_' || dsdt[i + 1] != 'S' || dsdt[i + 2] != '5' || dsdt[i + 3] != '_')
            continue;

        /* Preceded by a NameOp (0x08), possibly with a root prefix ('\\'). */
        if (!(i >= 1 && dsdt[i - 1] == 0x08) && !(i >= 2 && dsdt[i - 2] == 0x08 && dsdt[i - 1] == '\\'))
            continue;

        size_t at = i + 4;

        if (at + 2 > length || dsdt[at] != 0x12) /* PackageOp */
            continue;

        at++;

        /* PkgLength: the top two bits of its first byte say how many bytes follow. */
        at += 1 + (dsdt[at] >> 6);

        if (at + 1 > length)
            continue;

        at++; /* NumElements */

        if (at < length && dsdt[at] == 0x0A) /* BytePrefix */
            at++;

        if (at >= length)
            continue;

        uint8_t first = dsdt[at++];

        if (at < length && dsdt[at] == 0x0A)
            at++;

        if (at >= length)
            continue;

        *type_a = first;
        *type_b = dsdt[at];

        return 0;
    }

    return -1;
}
