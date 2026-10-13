/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MEMORY_PAGING_ENTRY_H
#define KERNEL_MEMORY_PAGING_ENTRY_H

#include <stdint.h>

/*
 * Hardware-independent helpers for i386 32-bit paging (no PAE).
 *
 * A linear address is split into three fields
 * (Intel SDM Vol.3A 4.3, Figure 4-2):
 *
 *   31        22 21        12 11          0
 *   +-----------+-----------+-------------+
 *   | dir index | tbl index |   offset    |
 *   +-----------+-----------+-------------+
 */

/* Both the page directory and a page table hold 1024 4-byte entries. */
#define PAGING_ENTRIES_PER_TABLE 1024

/* Index into the page directory: bits 31:22. */
uint32_t paging_dir_index(uintptr_t virt);

/* Index into a page table: bits 21:12. */
uint32_t paging_table_index(uintptr_t virt);

#endif
