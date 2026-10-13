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
