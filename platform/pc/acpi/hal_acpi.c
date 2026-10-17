/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/acpi.h"

#include "acpi.h"
#include "acpi_parse.h"

#include "platform/pc/boot/multiboot.h"
#include "platform/pc/vmap/vmap.h"

#include "kernel/log/log.h"
#include "kernel/printf/printf.h"

/*
 * Finding the RSDP: the firmware puts it on a 16-byte boundary in the
 * BIOS area 0xE0000..0xFFFFF, or in the first KiB of the EBDA (ACPI
 * Specification 6.x, 5.2.5.1; OSDev Wiki "RSDP"). Only the BIOS area
 * is searched: the EBDA lives in page 0, which stays unmapped as the
 * null pointer guard. QEMU's firmware (SeaBIOS) uses the BIOS area.
 */
#define BIOS_AREA_START 0xE0000u
#define BIOS_AREA_END 0x100000u

static hal_acpi_info_t info;
static int info_valid;

const hal_acpi_info_t *hal_acpi_info(void)
{
    return info_valid ? &info : NULL;
}

static const acpi_rsdp_t *find_rsdp(void)
{
    /* A Multiboot2 loader hands over a copy (the only way under UEFI). */
    const acpi_rsdp_t *given = multiboot_rsdp();

    if (given != NULL && acpi_rsdp_valid(given))
        return given;

    for (uintptr_t address = BIOS_AREA_START; address < BIOS_AREA_END; address += 16)
    {
        const acpi_rsdp_t *rsdp = (const acpi_rsdp_t *)address;

        if (acpi_rsdp_valid(rsdp))
            return rsdp;
    }

    return NULL;
}

/* Map a table (header first for the length, then all of it) and verify it. */
static const acpi_table_header_t *map_table(uint32_t phys)
{
    const acpi_table_header_t *header = vmap(phys, sizeof(*header), 0);

    if (header == NULL)
        return NULL;

    uint32_t length = header->length;

    if (length < sizeof(*header) || length > 1024 * 1024)
        return NULL;

    header = vmap(phys, length, 0);

    if (header == NULL || acpi_checksum(header, length) != 0)
        return NULL;

    return header;
}

int acpi_init(void)
{
    const acpi_rsdp_t *rsdp = find_rsdp();

    if (rsdp == NULL)
        return -1;

    const acpi_table_header_t *rsdt = map_table(rsdp->rsdt_address);

    if (rsdt == NULL || rsdt->signature[0] != 'R' || rsdt->signature[1] != 'S' ||
        rsdt->signature[2] != 'D' || rsdt->signature[3] != 'T')
        return -1;

    info.revision = rsdp->revision;

    for (int i = 0; i < 6; i++)
        info.oem_id[i] = rsdt->oem_id[i];

    info.oem_id[6] = '\0';

    uint32_t entries = (rsdt->length - sizeof(*rsdt)) / 4;
    const uint8_t *list = (const uint8_t *)rsdt + sizeof(*rsdt);

    int found_madt = 0;

    for (uint32_t i = 0; i < entries; i++)
    {
        uint32_t phys = (uint32_t)list[4 * i] | (uint32_t)list[4 * i + 1] << 8 |
                        (uint32_t)list[4 * i + 2] << 16 | (uint32_t)list[4 * i + 3] << 24;
        const acpi_table_header_t *table = map_table(phys);


        if (table == NULL)
            continue;


        if (table->signature[0] == 'A' && table->signature[1] == 'P' && table->signature[2] == 'I' &&
            table->signature[3] == 'C')
        {
            if (acpi_parse_madt((const uint8_t *)table, table->length, &info) == 0)
                found_madt = 1;
            else
                kernel_log(LOG_WARN, "ACPI: malformed MADT");
        }
        else if (table->signature[0] == 'F' && table->signature[1] == 'A' && table->signature[2] == 'C' &&
                 table->signature[3] == 'P')
        {
            if (acpi_parse_fadt((const uint8_t *)table, table->length, &info) == 0 && info.dsdt_address != 0)
            {
                /* The DSDT holds the \_S5 object: the numbers that write "soft off" to PM1a_CNT. */
                const acpi_table_header_t *dsdt = map_table(info.dsdt_address);


                if (dsdt != NULL)
                    info.s5_valid = acpi_find_s5((const uint8_t *)dsdt, dsdt->length, &info.s5_type_a, &info.s5_type_b) == 0;
            }
        }
    }

    if (found_madt)
    {
        info_valid = 1;

        return 0;
    }

    return -1;
}
