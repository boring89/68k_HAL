/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/mmu.h"

#include "paging.h"
#include "paging_entry.h"

int hal_mmu_map(uintptr_t virt, uintptr_t phys, uint32_t flags)
{
    uint32_t paging_flags;

    if (paging_flags_from_hal(flags, &paging_flags) != 0)
        return -1;

    return paging_map(virt, phys, paging_flags);
}

int hal_mmu_unmap(uintptr_t virt)
{
    return paging_unmap(virt);
}

int hal_mmu_protect(uintptr_t virt, uint32_t flags)
{
    uint32_t paging_flags;

    if (paging_flags_from_hal(flags, &paging_flags) != 0)
        return -1;

    return paging_protect(virt, paging_flags);
}

int hal_mmu_get_flags(uintptr_t virt, uint32_t *flags)
{
    uint32_t entry_flags;

    if (paging_get_flags(virt, &entry_flags) != 0 || flags == 0)
        return -1;

    *flags = 0;

    if (entry_flags & PAGING_WRITABLE)
        *flags |= HAL_MMU_WRITABLE;

    if (entry_flags & PAGING_USER)
        *flags |= HAL_MMU_USER;

    if (entry_flags & PAGING_CACHE_DISABLE)
        *flags |= HAL_MMU_UNCACHED;

    return 0;
}

int hal_mmu_get_phys(uintptr_t virt, uintptr_t *phys)
{
    return paging_get_phys(virt, phys);
}
