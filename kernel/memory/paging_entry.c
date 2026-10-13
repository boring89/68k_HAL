/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "paging_entry.h"

#define PAGING_DIR_SHIFT 22
#define PAGING_TABLE_SHIFT 12
#define PAGING_INDEX_MASK (PAGING_ENTRIES_PER_TABLE - 1)

uint32_t paging_dir_index(uintptr_t virt)
{
    /* The mask keeps the result in range when uintptr_t is 64-bit (host). */
    return (uint32_t)(virt >> PAGING_DIR_SHIFT) & PAGING_INDEX_MASK;
}

uint32_t paging_table_index(uintptr_t virt)
{
    return (uint32_t)(virt >> PAGING_TABLE_SHIFT) & PAGING_INDEX_MASK;
}
