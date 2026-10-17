/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "vmap.h"

#include "hal/mmu.h"

static uintptr_t next_virt = VMAP_BASE;

void *vmap(uint64_t phys, size_t length, uint32_t flags)
{
    if (length == 0)
        return NULL;

#ifndef HAL_PHYS_BASE
    /* The 32-bit kernel cannot address anything above 4 GiB. */
    if (phys + length > 0x100000000ull)
        return NULL;
#endif

    uintptr_t offset = (uintptr_t)(phys & (HAL_MMU_PAGE_SIZE - 1));
    uintptr_t first = (uintptr_t)(phys - offset);
    uintptr_t pages = (offset + length + HAL_MMU_PAGE_SIZE - 1) / HAL_MMU_PAGE_SIZE;

    if (pages > (VMAP_LIMIT - next_virt) / HAL_MMU_PAGE_SIZE)
        return NULL;

    uintptr_t virt = next_virt;

    for (uintptr_t i = 0; i < pages; i++)
    {
        if (hal_mmu_map(virt + i * HAL_MMU_PAGE_SIZE,
                        first + i * HAL_MMU_PAGE_SIZE, flags) != 0)
        {
            while (i-- > 0)
                hal_mmu_unmap(virt + i * HAL_MMU_PAGE_SIZE);

            return NULL;
        }
    }

    next_virt += pages * HAL_MMU_PAGE_SIZE;

    return (void *)(virt + offset);
}
