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

/*
 * Physical memory as the kernel sees it. On a 32-bit machine the boot data and the boot modules
 * are identity mapped (virtual == physical). A 64-bit kernel maps all RAM at a fixed offset, the
 * "higher half direct map" (HAL_PHYS_BASE, set by the build for the x86_64 kernel; see
 * docs/x86_64.md). hal_phys_to_virt (and the macro HAL_PHYS_TO_VIRT, usable in a static initializer) gives the pointer through which a physical address
 * the boot loader handed over (a module, a table) can be read; it is only valid for RAM and for
 * addresses that the boot data mapping covers.
 */
#ifdef HAL_PHYS_BASE
#define HAL_PHYS_TO_VIRT(phys) ((void *)(uintptr_t)((uint64_t)(phys) + HAL_PHYS_BASE))
#else
#define HAL_PHYS_TO_VIRT(phys) ((void *)(uintptr_t)(phys))
#endif

static inline void *hal_phys_to_virt(uint64_t phys)
{
    return HAL_PHYS_TO_VIRT(phys);
}

/* Mapping flags. Without HAL_MMU_WRITABLE a page is read-only. */
#define HAL_MMU_WRITABLE 0x1u
#define HAL_MMU_USER     0x2u /* reachable from user mode */
#define HAL_MMU_UNCACHED 0x4u /* device memory: never cached */
/*
 * Display memory: not cached, but writes are collected and sent in bursts (write combining), which is
 * many times faster than HAL_MMU_UNCACHED for a framebuffer. Reads are as slow as uncached ones:
 * keep a copy in RAM. Without PAT support the page is write-through. Not together with HAL_MMU_UNCACHED.
 */
#define HAL_MMU_WRITE_COMBINING 0x8u

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

/*
 * Address spaces. Every user process has its own: the kernel's mappings are the same in all of
 * them, the addresses the architecture reserves for user programs (x86: 0x08000000-0x0FFFFFFF
 * and 0x50000000-0x7FFFFFFF) belong to one space each. hal_mmu_* above always works on the
 * current space. A handle of 0 is never valid.
 */
typedef uintptr_t hal_space_t;

/* The space the CPU is using now. */
hal_space_t hal_space_current(void);

/* A new space with the kernel's mappings and nothing else, or 0 if memory or table slots ran out (see HAL_SPACE_MAX). */
hal_space_t hal_space_create(void);

/* Make space the current one. */
void hal_space_switch(hal_space_t space);

/*
 * Free a space's page tables and directory. space must not be the current one, and the user
 * frames mapped in it must have been released already (this only frees the tables). Returns 0 or -1.
 */
int hal_space_destroy(hal_space_t space);

/* Like hal_mmu_map, in any space (not only the current one). Only user addresses may be mapped in another space. */
int hal_space_map(hal_space_t space, uintptr_t virt, uintptr_t phys, uint32_t flags);

/*
 * Copy the page at virt of the current space into a new frame and map that frame at virt in
 * space dst with the same flags (fork). Returns 0 or -1 (out of memory, already mapped).
 */
int hal_space_copy_page(hal_space_t dst, uintptr_t virt);

/* Most spaces that exist at the same time, kernel's included. */
#define HAL_SPACE_MAX 72

#endif
