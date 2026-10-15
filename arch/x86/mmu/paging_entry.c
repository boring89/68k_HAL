/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "paging_entry.h"

#include "hal/mmu.h"

#define PAGING_DIR_SHIFT 22
#define PAGING_TABLE_SHIFT 12
#define PAGING_INDEX_MASK (PAGING_ENTRIES_PER_TABLE - 1)

/* Frames are 4 KiB aligned, so bits 11:0 of an entry are free for flags. */
#define PAGING_ADDRESS_MASK 0xFFFFF000u
#define PAGING_FLAGS_MASK 0x00000FFFu

uint32_t paging_dir_index(uintptr_t virt)
{
    /* The mask keeps the result in range when uintptr_t is 64-bit (host). */
    return (uint32_t)(virt >> PAGING_DIR_SHIFT) & PAGING_INDEX_MASK;
}

uint32_t paging_table_index(uintptr_t virt)
{
    return (uint32_t)(virt >> PAGING_TABLE_SHIFT) & PAGING_INDEX_MASK;
}

paging_entry_t paging_entry_make(uintptr_t phys, uint32_t flags)
{
    return ((uint32_t)phys & PAGING_ADDRESS_MASK) | (flags & PAGING_FLAGS_MASK);
}

uintptr_t paging_entry_get_phys(paging_entry_t entry)
{
    return entry & PAGING_ADDRESS_MASK;
}

int paging_flags_from_hal(uint32_t hal_flags, uint32_t *flags)
{
    if (hal_flags & ~(HAL_MMU_WRITABLE | HAL_MMU_USER))
        return -1;

    uint32_t result = 0;

    if (hal_flags & HAL_MMU_WRITABLE)
        result |= PAGING_WRITABLE;

    if (hal_flags & HAL_MMU_USER)
        result |= PAGING_USER;

    *flags = result;

    return 0;
}

int paging_entry_is_present(paging_entry_t entry)
{
    return (entry & PAGING_PRESENT) != 0;
}

uintptr_t paging_table_virt(uint32_t dir_index)
{
    /* Each page table occupies one 4 KiB page of the top 4 MiB. */
    return PAGING_TABLES_VIRT
        + ((uintptr_t)(dir_index & PAGING_INDEX_MASK) << PAGING_TABLE_SHIFT);
}
