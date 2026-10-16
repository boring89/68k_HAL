/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_MMU_PAGING_ENTRY_H
#define ARCH_X86_MMU_PAGING_ENTRY_H

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

/*
 * Entry flags, shared by PDEs and PTEs
 * (Intel SDM Vol.3A 4.3, Table 4-5 and Table 4-6).
 */
#define PAGING_PRESENT  0x001 /* bit 0: P */
#define PAGING_WRITABLE 0x002 /* bit 1: R/W */
#define PAGING_USER     0x004 /* bit 2: U/S */
#define PAGING_WRITE_THROUGH 0x008 /* bit 3: PWT */
#define PAGING_CACHE_DISABLE 0x010 /* bit 4: PCD; with PWT = strong uncacheable (SDM Vol.3A 11.3) */

/*
 * One PDE or PTE:
 *
 *   31                     12 11          0
 *   +------------------------+-------------+
 *   | frame address >> 12    |    flags    |
 *   +------------------------+-------------+
 */
typedef uint32_t paging_entry_t;

/*
 * Recursive mapping: the last PDE points to the page directory itself,
 * so once paging is on, page table N is visible at
 * PAGING_TABLES_VIRT + N * 4096 and the directory at
 * PAGING_DIRECTORY_VIRT (OSDev Wiki "Paging", Recursive mapping).
 */
#define PAGING_RECURSIVE_INDEX 1023
#define PAGING_TABLES_VIRT     0xFFC00000u /* PAGING_RECURSIVE_INDEX << 22 */
#define PAGING_DIRECTORY_VIRT  0xFFFFF000u /* page table 1023 = directory */

/* Index into the page directory: bits 31:22. */
uint32_t paging_dir_index(uintptr_t virt);

/* Index into a page table: bits 21:12. */
uint32_t paging_table_index(uintptr_t virt);

/*
 * Build an entry. phys is truncated to a 4 KiB boundary and flags to
 * bits 11:0, so neither can corrupt the other; callers must check
 * alignment themselves if a misaligned phys is an error.
 */
paging_entry_t paging_entry_make(uintptr_t phys, uint32_t flags);

/* Frame address stored in an entry (bits 31:12). */
uintptr_t paging_entry_get_phys(paging_entry_t entry);

/*
 * Convert HAL_MMU_* flags (hal/mmu.h) to PAGING_* entry flags in *flags.
 * Returns -1 and leaves *flags unchanged if hal_flags has unknown bits.
 */
int paging_flags_from_hal(uint32_t hal_flags, uint32_t *flags);

/* Nonzero if the P bit is set. */
int paging_entry_is_present(paging_entry_t entry);

/*
 * Virtual address of page table dir_index through the recursive mapping
 * (only meaningful while paging is on). dir_index is taken modulo 1024.
 */
uintptr_t paging_table_virt(uint32_t dir_index);

#endif
