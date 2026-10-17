/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/mmu.h"

#include "paging.h"

#include "arch/x86/cpu/cpu.h"
#include "kernel/lib/k_memory.h"
#include "kernel/memory/pmm.h"

/*
 * Address spaces (hal_space_*, see docs/process.md). A space is a PML4. Its upper half is a copy
 * of the kernel's (the three entries paging_init made, so the kernel's tables exist once); its
 * lower half is private to the space. Every page table is reached through the direct map, so the
 * tables of a space that is not the current one can be edited directly.
 */

#define USER_HALF_ENTRIES 256

static unsigned int space_count;

static uint64_t *table_at(uint64_t phys)
{
    return (uint64_t *)hal_phys_to_virt(phys);
}

hal_space_t hal_space_current(void)
{
    return (hal_space_t)(cpu_read_cr3() & PAGING64_ADDRESS_MASK);
}

hal_space_t hal_space_create(void)
{
    if (paging_kernel_root() == 0 || space_count + 1 >= HAL_SPACE_MAX)
        return 0;

    uint64_t frame = paging_alloc_table();

    if (frame == 0)
        return 0;

    const uint64_t *kernel = table_at(paging_kernel_root());
    uint64_t *root = table_at(frame);

    for (unsigned int i = USER_HALF_ENTRIES; i < PAGING64_ENTRIES_PER_TABLE; i++)
        root[i] = kernel[i];

    space_count++;

    return (hal_space_t)frame;
}

void hal_space_switch(hal_space_t space)
{
    cpu_write_cr3(space);
}

/* Free the tables below a table entry (not the frames the leaves map). */
static void free_tables(uint64_t entry, unsigned int level)
{
    if (!(entry & PAGING64_PRESENT) || (level <= 3 && (entry & PAGING64_LARGE)))
        return;

    uint64_t phys = paging64_entry_phys(entry);

    if (level > 1)
    {
        const uint64_t *table = table_at(phys);

        for (unsigned int i = 0; i < PAGING64_ENTRIES_PER_TABLE; i++)
            free_tables(table[i], level - 1);
    }

    /* level 1: this is the page table itself; the frames it maps belong to the caller. */
    pmm_free_frame(phys);
}

int hal_space_destroy(hal_space_t space)
{
    if (space == 0 || space == paging_kernel_root() || space == hal_space_current() || space_count == 0)
        return -1;

    const uint64_t *root = table_at(space);

    for (unsigned int i = 0; i < USER_HALF_ENTRIES; i++)
        free_tables(root[i], 3);

    pmm_free_frame(space);
    space_count--;

    return 0;
}

int hal_space_map(hal_space_t space, uintptr_t virt, uintptr_t phys, uint32_t flags)
{
    uint64_t paging_flags;

    if (paging64_flags_from_hal(flags, paging_nx_enabled(), &paging_flags) != 0)
        return -1;

    if (space == hal_space_current())
        return paging_map(virt, phys, paging_flags);

    /* Another space: only its private (user) addresses. */
    if (paging64_is_kernel_address(virt))
        return -1;

    return paging_map_in(space, virt, phys, paging_flags);
}

int hal_space_copy_page(hal_space_t dst, uintptr_t virt)
{
    uint64_t flags;

    if (paging_get_flags(virt, &flags) != 0)
        return -1;

    uint64_t frame = pmm_alloc_frame();

    if (frame == 0)
        return -1;

    k_memcpy(table_at(frame), (const void *)virt, PMM_FRAME_SIZE);

    if (paging_map_in(dst, virt, frame, flags & ~(PAGING64_ACCESSED | PAGING64_DIRTY)) != 0)
    {
        pmm_free_frame(frame);

        return -1;
    }

    return 0;
}
