/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_MMU_H
#define HAL_MMU_H

#include <stdint.h>

/*
 * Virtual memory mappings of the current address space, one 4 KiB page
 * at a time. The architecture turns paging on during start-up.
 * Implemented once per architecture (arch/<arch>/mmu/hal_mmu.c).
 */

#define HAL_MMU_PAGE_SIZE 4096

/* Mapping flags. Without HAL_MMU_WRITABLE a page is read-only. */
#define HAL_MMU_WRITABLE 0x1u
#define HAL_MMU_USER     0x2u /* reachable from user mode */
#define HAL_MMU_UNCACHED 0x4u /* device memory: never cached */

/*
 * Map the page at virt to the frame at phys. Returns -1 if either is
 * not page aligned, flags has unknown bits, virt is already mapped or
 * reserved by the architecture, or no memory is left for page tables.
 * Does not take ownership of phys.
 */
int hal_mmu_map(uintptr_t virt, uintptr_t phys, uint32_t flags);

/*
 * Remove the mapping of the page at virt. The frame is not freed.
 * Returns -1 if virt is not page aligned, reserved or not mapped.
 */
int hal_mmu_unmap(uintptr_t virt);

/*
 * Change the flags of the page at virt (which must be mapped) to flags
 * (HAL_MMU_* bits); the frame stays. Returns -1 if virt is not page
 * aligned, flags has unknown bits, or virt is not mapped or is reserved by
 * the architecture. Use it to make loaded code read-only or to turn a page
 * into a guard page (flags 0: only the kernel can reach it).
 */
int hal_mmu_protect(uintptr_t virt, uint32_t flags);

/* The flags the page at virt is mapped with. Returns 0, or -1 if it is not mapped. */
int hal_mmu_get_flags(uintptr_t virt, uint32_t *flags);

/*
 * Translate virt. On success stores the physical address (including the
 * offset within the page) in *phys and returns 0; returns -1 and leaves
 * *phys unchanged if virt is not mapped or phys is NULL.
 */
int hal_mmu_get_phys(uintptr_t virt, uintptr_t *phys);

#endif
