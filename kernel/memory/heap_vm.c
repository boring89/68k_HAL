/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "heap.h"
#include "pmm.h"

#include "hal/mmu.h"

#include "kernel/panic/panic.h"

/* Grow in steps of at least 64 KiB so small allocations do not map a page each. */
#define HEAP_GROW_STEP (16 * HAL_MMU_PAGE_SIZE)

/*
 * Map enough fresh frames at address (page aligned: the heap end only moves
 * by whole pages) to give min_bytes. The frames need not be contiguous:
 * paging makes them one range. On failure everything mapped here is undone.
 */
static size_t heap_vm_grow(uintptr_t address, size_t min_bytes)
{
    size_t bytes = (min_bytes + HEAP_GROW_STEP - 1) / HEAP_GROW_STEP *
                   HEAP_GROW_STEP;

    if (bytes < min_bytes || address % HAL_MMU_PAGE_SIZE != 0)
        return 0;

    for (size_t offset = 0; offset < bytes; offset += HAL_MMU_PAGE_SIZE)
    {
        uintptr_t frame = pmm_alloc_frame();

        if (frame != 0 &&
            hal_mmu_map(address + offset, frame, HAL_MMU_WRITABLE) == 0)
            continue;

        if (frame != 0)
            pmm_free_frame(frame);

        /* Undo the pages mapped so far. */
        while (offset > 0)
        {
            uintptr_t phys;

            offset -= HAL_MMU_PAGE_SIZE;

            if (hal_mmu_get_phys(address + offset, &phys) == 0)
            {
                hal_mmu_unmap(address + offset);
                pmm_free_frame(phys & ~(uintptr_t)(HAL_MMU_PAGE_SIZE - 1));
            }
        }

        return 0;
    }

    return bytes;
}

void heap_vm_init(void)
{
    heap_init(HEAP_VIRT_BASE, 0);
    heap_set_grow(heap_vm_grow, HEAP_VIRT_LIMIT);

    /* kmalloc would grow it on demand; mapping the first part now makes a
     * shortage of memory show up at boot instead. */
    void *probe = kmalloc(KERNEL_HEAP_SIZE - 64);

    if (probe == 0)
        panic("heap: cannot map the first %u bytes",
              (unsigned int)KERNEL_HEAP_SIZE);

    kfree(probe);
}
