/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MEMORY_PAGING_H
#define KERNEL_MEMORY_PAGING_H

#include <stdint.h>

#include "paging_entry.h"

/*
 * i386 32-bit paging with 4 KiB pages (see docs/paging.md).
 *
 * The first 4 MiB are identity mapped (virtual == physical) except
 * page 0, so null pointer accesses fault. The page tables are reached
 * through the recursive mapping in directory entry 1023.
 */

/* End of the identity mapped region set up by paging_init. */
#define PAGING_IDENTITY_END 0x00400000u

/*
 * Build the page directory and the first page table from PMM frames,
 * load CR3 and turn on paging. Must run after pmm_init, heap_init and
 * idt_init. Panics if the kernel or heap lies outside the first 4 MiB.
 */
void paging_init(void);

/*
 * Translate virt the way the MMU would. On success stores the physical
 * address (including the offset within the page) in *phys and returns
 * 0; returns -1 and leaves *phys unchanged if virt is not mapped.
 */
int paging_get_phys(uintptr_t virt, uintptr_t *phys);

#endif
