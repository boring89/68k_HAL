/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_MACDISK_H
#define KERNEL_FS_MACDISK_H

#include <stdint.h>

#include "kernel/block/block.h"
#include "kernel/block/overlay.h"

/*
 * The 68k_HAL disk layout (docs/mac_disk.md): a GPT disk with Boot, System A,
 * System B, Overlay and Applications partitions. macdisk_init finds it among
 * the block devices, makes each partition a block device (boot, sys_a,
 * sys_b, ovl_store, apps) and opens the system overlay, which appears as the
 * block device "system": the Mac's system disk (the active System slot with
 * the Mac's writes on top).
 */

#define MACDISK_SLOT_A 0
#define MACDISK_SLOT_B 1

/*
 * Look for the layout and set it up with slot (MACDISK_SLOT_A or _B) as the
 * active System slot. Returns 0 if a layout was found (the overlay may still
 * be unusable: see macdisk_overlay_status), -1 if there is none.
 */
int macdisk_init(uint32_t slot);

/*
 * The slot a kernel command line asks for: "slot=b" means System B, anything
 * else (or NULL) System A.
 */
uint32_t macdisk_slot_from_cmdline(const char *cmdline);

/* 1 once macdisk_init found a layout. */
int macdisk_present(void);

uint32_t macdisk_slot(void);

/* The block devices; NULL if absent. system is NULL unless the overlay opened. */
block_device_t *macdisk_system(void);
block_device_t *macdisk_apps(void);
block_device_t *macdisk_boot(void);
block_device_t *macdisk_slot_device(uint32_t slot);
block_device_t *macdisk_overlay_store(void);

overlay_status_t macdisk_overlay_status(void);
const overlay_t *macdisk_overlay(void);

/*
 * Reset the system overlay: the Mac's changes to the system disk are gone,
 * and a mismatching overlay is bound to the active slot as it is now.
 * Returns 0, or -1 (no layout, I/O error).
 */
int macdisk_reset(void);

/* Forget everything (for tests). */
void macdisk_forget(void);

#endif
