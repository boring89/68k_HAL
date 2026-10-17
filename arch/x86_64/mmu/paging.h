/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_64_MMU_PAGING_H
#define ARCH_X86_64_MMU_PAGING_H

#include <stdint.h>

#include "paging_entry.h"

/*
 * x86-64 4-level paging with 4 KiB pages (docs/x86_64.md, Intel SDM Vol.3A 4.5).
 *
 * Address space layout (PML4 index in brackets):
 *
 *   0x0000000000000000 - 0x00007FFFFFFFFFFF  user programs, one set of tables per space  [0-255]
 *   0xFFFF800000000000  direct map: all RAM at HAL_PHYS_BASE + physical address          [256]
 *   0xFFFFC00000000000  the kernel's windows: heap, guest RAM, vmap                      [384]
 *   0xFFFFFFFF80000000  the kernel image (physical + HAL_KERNEL_OFFSET)                  [511]
 *
 * The three upper-half PML4 entries are created once, by paging_init, and copied into every
 * address space, so the tables below them (the kernel's mappings) exist once. All page tables are
 * reached through the direct map; there is no recursive mapping.
 */

#define PAGING_HHDM_SLOT 256
#define PAGING_WINDOW_SLOT 384
#define PAGING_KERNEL_SLOT 511

/*
 * Build the kernel's page tables from PMM frames (the direct map of all RAM with 2 MiB pages, the
 * kernel image with 4 KiB pages and its sections' permissions) and switch to them. Runs on the
 * boot loader's temporary tables (platform/pc/boot/boot64.asm), which map the first 4 GiB; so
 * the frames it takes from the PMM must lie below that (the PMM hands out low frames first).
 * Must run after pmm_init and idt_init.
 */
void paging_init(void);

/*
 * Translate virt the way the MMU would (also through 2 MiB and 1 GiB pages). On success stores
 * the physical address (including the offset within the page) in *phys and returns 0; returns -1
 * and leaves *phys unchanged if virt is not mapped.
 */
int paging_get_phys(uintptr_t virt, uintptr_t *phys);

/*
 * Map the 4 KiB page at virt to the frame at phys in the current address space. PAGING64_PRESENT
 * is always added to flags. Creates the tables below the PML4 if needed. Returns -1 if virt or
 * phys is not 4 KiB aligned, virt is not canonical, virt is already mapped, virt is in the upper
 * half outside the PML4 entries paging_init created, or the PMM is out of frames for a table.
 * Does not take ownership of phys.
 */
int paging_map(uintptr_t virt, uint64_t phys, uint64_t flags);

/*
 * Remove the mapping of the 4 KiB page at virt and flush its TLB entry. The frame is not returned
 * to the PMM and empty tables are kept. Returns -1 if virt is not 4 KiB aligned or not mapped by
 * a 4 KiB page (the direct map and its 2 MiB pages cannot be changed).
 */
int paging_unmap(uintptr_t virt);

/* Replace the flags of a mapped 4 KiB page (PAGING64_* bits; PAGING64_PRESENT is kept). Returns 0 or -1. */
int paging_protect(uintptr_t virt, uint64_t flags);

/* The flags of a mapped page (without the frame address). Returns 0 or -1. */
int paging_get_flags(uintptr_t virt, uint64_t *flags);

/* True if the page tables mark pages as not executable (the CPU has NX and EFER.NXE is set). */
int paging_nx_enabled(void);

/* The PML4 of the kernel (physical address). */
uint64_t paging_kernel_root(void);

/*
 * The same as paging_map in the address space whose PML4 is at root (a physical address). Used
 * by space.c to fill another space.
 */
int paging_map_in(uint64_t root, uintptr_t virt, uint64_t phys, uint64_t flags);

/* A zeroed frame for a page table, or 0. */
uint64_t paging_alloc_table(void);

#endif
