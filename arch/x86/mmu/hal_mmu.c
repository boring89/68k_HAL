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

int hal_mmu_get_phys(uintptr_t virt, uintptr_t *phys)
{
    return paging_get_phys(virt, phys);
}
