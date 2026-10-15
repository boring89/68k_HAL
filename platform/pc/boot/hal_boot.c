/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/boot.h"

#include "multiboot.h"

#include <stddef.h>

/* Multiboot memory map types are the ACPI types, so they pass through. */
int hal_boot_memory_region(unsigned int index, hal_memory_region_t *region)
{
    if (region == NULL)
        return -1;

    const multiboot_mmap_entry_t *entry = multiboot_mmap_first();

    for (unsigned int i = 0; i < index && entry != NULL; i++)
        entry = multiboot_mmap_next(entry);

    if (entry == NULL)
        return -1;

    region->base = entry->addr;
    region->length = entry->len;
    region->type = entry->type;

    return 0;
}

/* Region 0 is the Multiboot info structure, region 1 the memory map. */
int hal_boot_reserved_region(unsigned int index, hal_memory_region_t *region)
{
    if (region == NULL)
        return -1;

    const multiboot_info_t *mbi = multiboot_get_info();

    if (index == 0)
    {
        region->base = (uintptr_t)mbi;
        region->length = sizeof(*mbi);
    }
    else if (index == 1 && (mbi->flags & MULTIBOOT_INFO_MEM_MAP))
    {
        region->base = mbi->mmap_addr;
        region->length = mbi->mmap_length;
    }
    else
    {
        return -1;
    }

    region->type = HAL_MEMORY_RESERVED;

    return 0;
}
