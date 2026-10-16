/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_VMAP_VMAP_H
#define PLATFORM_PC_VMAP_VMAP_H

#include <stddef.h>
#include <stdint.h>

/*
 * A window of virtual addresses for device memory and firmware tables
 * that lie outside the boot identity map: ACPI tables, the local APIC,
 * PCI BARs. Pages are handed out in order and never given back.
 */
#define VMAP_BASE 0xE0000000u
#define VMAP_LIMIT 0xF0000000u

/*
 * Map length bytes at physical address phys (any alignment) and return
 * a pointer to the first byte, or NULL if the window is full or the
 * mapping fails. flags are HAL_MMU_* flags (hal/mmu.h); use
 * HAL_MMU_UNCACHED for device registers.
 */
void *vmap(uint64_t phys, size_t length, uint32_t flags);

#endif
