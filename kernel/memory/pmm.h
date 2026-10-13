/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MEMORY_PMM_H
#define KERNEL_MEMORY_PMM_H

#include <stddef.h>
#include <stdint.h>

/*
 * Physical memory manager: one bit per 4 KiB frame (1 = used).
 *
 * Addresses are physical. 0 is never a valid frame (the low 1 MiB is
 * always reserved), so allocation functions return 0 on failure.
 *
 * Not IRQ safe: do not call from interrupt handlers.
 * Freeing cannot tell reserved frames from allocated ones; callers must
 * only free what they allocated.
 */

#define PMM_FRAME_SIZE 4096

/*
 * Build the bitmap from the Multiboot memory map (multiboot_init must
 * have run). Reserves the low 1 MiB, [kernel_start, kernel_end), the
 * Multiboot info structure and the memory map. Resets all state.
 */
void pmm_init(
    uintptr_t kernel_start,
    uintptr_t kernel_end);

/* Mark every frame touching [start, start + size) as used. */
int pmm_reserve_range(
    uintptr_t start,
    size_t size);

/* Allocate one frame; returns its address or 0. */
uintptr_t pmm_alloc_frame(void);

/* Allocate count contiguous frames; returns the first address or 0. */
uintptr_t pmm_alloc_frames(size_t count);

/* Free one frame; -1 if unaligned, out of range or already free. */
int pmm_free_frame(uintptr_t address);

/* Free count frames starting at address; -1 (nothing freed) on error. */
int pmm_free_frames(
    uintptr_t address,
    size_t count);

/* 1 if the frame containing address is used (or outside the bitmap's
 * range, so never allocatable), 0 if free. */
int pmm_is_frame_used(uintptr_t address);

/* Whole frames inside available (type 1) memory map regions. */
size_t pmm_get_total_frames(void);

size_t pmm_get_free_frames(void);

#endif
