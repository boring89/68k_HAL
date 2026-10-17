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

const char *hal_boot_cmdline(void)
{
    return multiboot_cmdline();
}

static const multiboot_module_t *module_table(void)
{
    const multiboot_info_t *mbi = multiboot_get_info();

    if (!(mbi->flags & MULTIBOOT_INFO_MODS) || mbi->mods_count == 0)
        return NULL;

    return (const multiboot_module_t *)(uintptr_t)mbi->mods_addr;
}

static size_t string_size(uint32_t address)
{
    const char *str = (const char *)(uintptr_t)address;
    size_t length = 0;

    while (str[length] != '\0')
        length++;

    return length + 1;
}

unsigned int hal_boot_module_count(void)
{
    return module_table() == NULL ? 0 : multiboot_get_info()->mods_count;
}

int hal_boot_module(unsigned int index, hal_boot_module_t *module)
{
    const multiboot_module_t *table = module_table();

    if (module == NULL || table == NULL || index >= hal_boot_module_count())
        return -1;

    module->base = table[index].mod_start;
    module->length = table[index].mod_end - table[index].mod_start;
    module->name = table[index].string != 0
                       ? (const char *)(uintptr_t)table[index].string
                       : "";

    return 0;
}

/*
 * Reserved, in this order: the Multiboot info structure, the Multiboot2 tag
 * list (if any), the memory map (if
 * any), then the module table (if any) followed by each module's contents
 * and name string.
 */
int hal_boot_reserved_region(unsigned int index, hal_memory_region_t *region)
{
    if (region == NULL)
        return -1;

    const multiboot_info_t *mbi = multiboot_get_info();

    region->type = HAL_MEMORY_RESERVED;

    if (index == 0)
    {
        region->base = (uintptr_t)mbi;
        region->length = sizeof(*mbi);
        return 0;
    }
    index--;

    /* Multiboot2: the loader's own tag list (module names point into it). */
    if (multiboot_raw_size() != 0)
    {
        if (index == 0)
        {
            region->base = multiboot_raw_base();
            region->length = multiboot_raw_size();
            return 0;
        }
        index--;
    }

    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP)
    {
        if (index == 0)
        {
            region->base = mbi->mmap_addr;
            region->length = mbi->mmap_length;
            return 0;
        }
        index--;
    }

    const multiboot_module_t *table = module_table();

    if (table == NULL)
        return -1;

    if (index == 0)
    {
        region->base = mbi->mods_addr;
        region->length = mbi->mods_count * sizeof(multiboot_module_t);
        return 0;
    }
    index--;

    unsigned int module = index / 2;

    if (module >= mbi->mods_count)
        return -1;

    if (index % 2 == 0)
    {
        region->base = table[module].mod_start;
        region->length = table[module].mod_end - table[module].mod_start;
    }
    else if (table[module].string != 0)
    {
        region->base = table[module].string;
        region->length = string_size(table[module].string);
    }
    else
    {
        /* No name: an empty region keeps the numbering regular. */
        region->base = 0;
        region->length = 0;
    }

    return 0;
}
