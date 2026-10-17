/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/dma.h"

#include "hal/mmu.h"

#include "platform/pc/vmap/vmap.h"

#include "kernel/lib/k_memory.h"
#include "kernel/memory/pmm.h"

/*
 * x86 PCI devices snoop the CPU caches (DMA is cache coherent), so the
 * memory is mapped as ordinary cached RAM. The frames come from the
 * PMM as one run (pmm_alloc_frames); the PC has no IOMMU here, so the
 * device sees physical addresses directly.
 */
void *hal_dma_alloc(size_t size, uint32_t *phys)
{
    size_t frames = (size + HAL_MMU_PAGE_SIZE - 1) / HAL_MMU_PAGE_SIZE;

    if (frames == 0 || phys == NULL)
        return NULL;

    uintptr_t address = pmm_alloc_frames(frames);

    if (address == 0)
        return NULL;

    /* Devices are given 32-bit addresses (hal/dma.h): the PMM hands out low frames first, but check. */
    if ((uint64_t)address + frames * HAL_MMU_PAGE_SIZE > 0x100000000ull)
    {
        pmm_free_frames(address, frames);

        return NULL;
    }

    void *memory = vmap(address, frames * HAL_MMU_PAGE_SIZE, HAL_MMU_WRITABLE);

    if (memory == NULL)
    {
        pmm_free_frames(address, frames);

        return NULL;
    }

    k_memset(memory, 0, frames * HAL_MMU_PAGE_SIZE);
    *phys = (uint32_t)address;

    return memory;
}

void hal_dma_free(void *memory, size_t size)
{
    size_t frames = (size + HAL_MMU_PAGE_SIZE - 1) / HAL_MMU_PAGE_SIZE;
    uintptr_t phys;

    if (memory == NULL || hal_mmu_get_phys((uintptr_t)memory, &phys) != 0)
        return;

    for (size_t i = 0; i < frames; i++)
        hal_mmu_unmap((uintptr_t)memory + i * HAL_MMU_PAGE_SIZE);

    pmm_free_frames(phys, frames);
}
