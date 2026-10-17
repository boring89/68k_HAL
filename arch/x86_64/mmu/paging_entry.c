/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "paging_entry.h"

#include "hal/mmu.h"

unsigned int paging64_index(uint64_t virt, unsigned int level)
{
    return (unsigned int)(virt >> (12 + 9 * (level - 1))) & (PAGING64_ENTRIES_PER_TABLE - 1);
}

paging64_entry_t paging64_entry_make(uint64_t phys, uint64_t flags)
{
    return (phys & PAGING64_ADDRESS_MASK) | (flags & ~PAGING64_ADDRESS_MASK);
}

uint64_t paging64_entry_phys(paging64_entry_t entry)
{
    return entry & PAGING64_ADDRESS_MASK;
}

int paging64_flags_from_hal(uint32_t hal_flags, bool no_execute, uint64_t *flags)
{
    if (hal_flags & ~(HAL_MMU_WRITABLE | HAL_MMU_USER | HAL_MMU_UNCACHED))
        return -1;

    uint64_t result = 0;

    if (hal_flags & HAL_MMU_WRITABLE)
        result |= PAGING64_WRITABLE;

    if (hal_flags & HAL_MMU_USER)
        result |= PAGING64_USER;
    else if (no_execute)
        result |= PAGING64_NO_EXECUTE; /* user programs keep "readable = executable" (docs/userland.md) */

    if (hal_flags & HAL_MMU_UNCACHED)
        result |= PAGING64_WRITE_THROUGH | PAGING64_CACHE_DISABLE;

    *flags = result;

    return 0;
}

uint32_t paging64_flags_to_hal(uint64_t flags)
{
    uint32_t result = 0;

    if (flags & PAGING64_WRITABLE)
        result |= HAL_MMU_WRITABLE;

    if (flags & PAGING64_USER)
        result |= HAL_MMU_USER;

    if (flags & PAGING64_CACHE_DISABLE)
        result |= HAL_MMU_UNCACHED;

    return result;
}

bool paging64_is_kernel_address(uint64_t virt)
{
    return (virt >> 47) != 0;
}
