/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_DMA_H
#define HAL_DMA_H

#include <stddef.h>
#include <stdint.h>

/*
 * Memory a device can read and write by itself (DMA, "direct memory
 * access"): network card descriptor rings, sound buffers. A device
 * needs the physical address, the CPU needs a virtual one; this gives
 * both for physically contiguous memory. Implemented once per
 * platform (platform/<platform>/dma/hal_dma.c).
 */

/*
 * Allocate size bytes (rounded up to whole pages), zeroed, page
 * aligned, physically contiguous and below 4 GiB. Stores the physical
 * address in *phys and returns the virtual address, or NULL if memory
 * is short. The memory is cache coherent with the device.
 */
void *hal_dma_alloc(size_t size, uint32_t *phys);

/* Give back what hal_dma_alloc returned (with the same size). */
void hal_dma_free(void *memory, size_t size);

#endif
