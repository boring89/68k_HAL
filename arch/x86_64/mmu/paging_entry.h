/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_64_MMU_PAGING_ENTRY_H
#define ARCH_X86_64_MMU_PAGING_ENTRY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Hardware-independent helpers for x86-64 4-level paging with 4 KiB pages (Intel SDM Vol.3A 4.5).
 *
 * A linear address is split into four 9-bit indices and an offset (Figure 4-8):
 *
 *   47      39 38      30 29      21 20      12 11          0
 *   +---------+----------+----------+----------+-------------+
 *   |  PML4   |   PDPT   |    PD    |    PT    |   offset    |
 *   +---------+----------+----------+----------+-------------+
 *    level 4    level 3    level 2    level 1
 *
 * Bits 63:48 repeat bit 47 (canonical addresses): the lower half (PML4 index 0-255) is for user
 * programs, the upper half (256-511) is the kernel's and is the same in every address space.
 */

#define PAGING64_ENTRIES_PER_TABLE 512

/* Entry flags, shared by all levels (SDM Vol.3A 4.5, Table 4-15 to 4-20). */
#define PAGING64_PRESENT (1ull << 0)
#define PAGING64_WRITABLE (1ull << 1)
#define PAGING64_USER (1ull << 2)
#define PAGING64_WRITE_THROUGH (1ull << 3)  /* PWT */
#define PAGING64_CACHE_DISABLE (1ull << 4)  /* PCD; with PWT = strong uncacheable */
#define PAGING64_ACCESSED (1ull << 5)
#define PAGING64_DIRTY (1ull << 6)
#define PAGING64_LARGE (1ull << 7)          /* PS in a PDPT/PD entry: a 1 GiB / 2 MiB page */
#define PAGING64_PAT (1ull << 7)            /* PAT in a 4 KiB PTE: the third index bit into IA32_PAT */
#define PAGING64_GLOBAL (1ull << 8)
#define PAGING64_NO_EXECUTE (1ull << 63)    /* XD; needs EFER.NXE */

/* The frame address of an entry is bits 51:12. */
#define PAGING64_ADDRESS_MASK 0x000FFFFFFFFFF000ull

typedef uint64_t paging64_entry_t;

/* Index of virt into the table of the given level (4 = PML4 ... 1 = page table). */
unsigned int paging64_index(uint64_t virt, unsigned int level);

/* Build an entry; phys is truncated to a 4 KiB boundary and bits 51:12, flags to the other bits. */
paging64_entry_t paging64_entry_make(uint64_t phys, uint64_t flags);

/* Frame address stored in an entry. */
uint64_t paging64_entry_phys(paging64_entry_t entry);

/*
 * Convert HAL_MMU_* flags (hal/mmu.h) to entry flags in *flags. Mappings of the kernel (without
 * HAL_MMU_USER) are not executable if no_execute is true. Returns -1 and leaves *flags unchanged
 * if hal_flags has unknown bits.
 */
int paging64_flags_from_hal(uint32_t hal_flags, bool no_execute, uint64_t *flags);

/* The HAL_MMU_* flags of an entry's flag bits. */
uint32_t paging64_flags_to_hal(uint64_t flags);

/* True if virt is in the upper (kernel) half. */
bool paging64_is_kernel_address(uint64_t virt);

#endif
