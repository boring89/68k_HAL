/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "paging.h"

#include "heap.h"
#include "memory.h"
#include "pmm.h"
#include "../lib/k_memory.h"
#include "../panic/panic.h"
#include "../../arch/x86/cpu/cpu.h"

/* Kernel-only, read/write: the only flags used before user mode exists. */
#define PAGING_KERNEL_FLAGS (PAGING_PRESENT | PAGING_WRITABLE)

void paging_init(void)
{
    /*
     * Everything the kernel touches must already be mapped the moment
     * CR0.PG is set, or the next instruction fetch triple faults.
     */
    KASSERT(memory_get_kernel_end() <= PAGING_IDENTITY_END);
    KASSERT(heap_get_start() + KERNEL_HEAP_SIZE <= PAGING_IDENTITY_END);

    uintptr_t directory_phys = pmm_alloc_frame();
    uintptr_t table_phys = pmm_alloc_frame();

    if (directory_phys == 0 || table_phys == 0)
        panic("paging: cannot allocate the initial page tables");

    /*
     * Paging is still off, so physical addresses can be used directly.
     * PMM frames hold stale data; zero means "not present" everywhere.
     */
    paging_entry_t *directory = (paging_entry_t *)directory_phys;
    paging_entry_t *table = (paging_entry_t *)table_phys;

    k_memset(directory, 0, PMM_FRAME_SIZE);
    k_memset(table, 0, PMM_FRAME_SIZE);

    /* Identity map 0x1000-0x3FFFFF; entry 0 (page 0) stays not present. */
    for (uint32_t i = 1; i < PAGING_ENTRIES_PER_TABLE; i++)
        table[i] = paging_entry_make(i * PMM_FRAME_SIZE, PAGING_KERNEL_FLAGS);

    directory[0] = paging_entry_make(table_phys, PAGING_KERNEL_FLAGS);
    directory[PAGING_RECURSIVE_INDEX] =
        paging_entry_make(directory_phys, PAGING_KERNEL_FLAGS);

    cpu_write_cr3((uint32_t)directory_phys);
    cpu_write_cr0(cpu_read_cr0() | CPU_CR0_PG);
}

/*
 * Return page table dir_index through the recursive mapping, creating
 * it from a zeroed PMM frame if needed. Returns 0 for the recursive
 * slot (the top 4 MiB is reserved) or if the PMM is out of frames.
 * Requires paging to be on.
 */
static paging_entry_t *paging_get_or_create_table(uint32_t dir_index)
{
    if (dir_index == PAGING_RECURSIVE_INDEX)
        return 0;

    paging_entry_t *directory = (paging_entry_t *)PAGING_DIRECTORY_VIRT;
    paging_entry_t *table = (paging_entry_t *)paging_table_virt(dir_index);

    if (paging_entry_is_present(directory[dir_index]))
        return table;

    uintptr_t table_phys = pmm_alloc_frame();

    if (table_phys == 0)
        return 0;

    /*
     * The new frame is only reachable through its recursive address
     * once the PDE exists, so link it first and zero it afterwards.
     * Restrictions go in the PTEs; the PDE stays permissive.
     */
    directory[dir_index] = paging_entry_make(table_phys, PAGING_KERNEL_FLAGS);

    /* Also drops any paging-structure cache entry (Intel SDM Vol.3A 4.10.4.1). */
    cpu_invlpg((uintptr_t)table);

    k_memset(table, 0, PMM_FRAME_SIZE);

    return table;
}

int paging_get_phys(uintptr_t virt, uintptr_t *phys)
{
    if (phys == 0)
        return -1;

    /* Walk the tables through the recursive mapping, like the MMU does. */
    paging_entry_t *directory = (paging_entry_t *)PAGING_DIRECTORY_VIRT;
    uint32_t dir_index = paging_dir_index(virt);

    if (!paging_entry_is_present(directory[dir_index]))
        return -1;

    /* Only valid after the PDE check: otherwise this address faults. */
    paging_entry_t *table = (paging_entry_t *)paging_table_virt(dir_index);
    paging_entry_t entry = table[paging_table_index(virt)];

    if (!paging_entry_is_present(entry))
        return -1;

    *phys = paging_entry_get_phys(entry) | (virt & (PMM_FRAME_SIZE - 1));
    return 0;
}

int paging_map(uintptr_t virt, uintptr_t phys, uint32_t flags)
{
    if (virt % PMM_FRAME_SIZE != 0 || phys % PMM_FRAME_SIZE != 0)
        return -1;

    paging_entry_t *table = paging_get_or_create_table(paging_dir_index(virt));

    if (table == 0)
        return -1;

    paging_entry_t *entry = &table[paging_table_index(virt)];

    /* Remapping silently is usually a bug: callers must unmap first. */
    if (paging_entry_is_present(*entry))
        return -1;

    *entry = paging_entry_make(phys, flags | PAGING_PRESENT);

    /*
     * Not-present entries are not cached, but a new table may have
     * briefly held stale data before it was zeroed (see above).
     */
    cpu_invlpg(virt);

    return 0;
}

int paging_unmap(uintptr_t virt)
{
    if (virt % PMM_FRAME_SIZE != 0)
        return -1;

    /* Unmapping here would cut off access to the page tables themselves. */
    uint32_t dir_index = paging_dir_index(virt);

    if (dir_index == PAGING_RECURSIVE_INDEX)
        return -1;

    paging_entry_t *directory = (paging_entry_t *)PAGING_DIRECTORY_VIRT;

    if (!paging_entry_is_present(directory[dir_index]))
        return -1;

    paging_entry_t *table = (paging_entry_t *)paging_table_virt(dir_index);
    paging_entry_t *entry = &table[paging_table_index(virt)];

    if (!paging_entry_is_present(*entry))
        return -1;

    *entry = 0;

    /* Without this the CPU may keep using the old translation. */
    cpu_invlpg(virt);

    return 0;
}
