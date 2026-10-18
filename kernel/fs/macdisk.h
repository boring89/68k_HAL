/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_MACDISK_H
#define KERNEL_FS_MACDISK_H

#include <stdint.h>

#include "kernel/block/block.h"
#include "kernel/block/overlay.h"
#include "kernel/fs/grubenv.h"

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

/* The slot a setting names: "b" or "B" is System B, anything else (or NULL) System A. */
uint32_t macdisk_slot_from_name(const char *name);

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

/*
 * The GRUB environment block on the Boot partition (68k_hal/grubenv): boot_slot (the confirmed kernel
 * slot), system_slot, trial (a kernel slot to boot once). Read and write in place (the file keeps its
 * sectors). Return 0, or -1 (no Boot partition, no such file, I/O error).
 */
int macdisk_env_read(grubenv_t *env);
int macdisk_env_write(const grubenv_t *env);

/*
 * The kernel started fine from kernel_slot ("a" or "b") after a trial boot (confirm != 0): make it the
 * confirmed slot (grubenv boot_slot), so that GRUB keeps booting it. Without confirm nothing is written.
 * Returns 1 if grubenv was changed, 0 if nothing had to be done, -1 on an error.
 */
int macdisk_confirm_kernel_slot(const char *kernel_slot, int confirm);

/* Results of macdisk_switch and macdisk_promote besides 0 and -1. */
#define MACDISK_OVERLAY_NOT_EMPTY (-2)
#define MACDISK_NOT_SAVED (-3)      /* done, but grubenv could not be written: the next boot uses the old slot */

/*
 * Use the other System slot from now on (and at the next boots: grubenv system_slot). The overlay is
 * bound to it empty; if it holds changes, nothing happens and MACDISK_OVERLAY_NOT_EMPTY is returned
 * unless force is set (the changes are then dropped). Returns 0, MACDISK_NOT_SAVED or -1.
 */
int macdisk_switch(uint32_t slot, int force);

/*
 * Copy the system disk as the Mac sees it (the active slot with the overlay's changes) into the other
 * System slot, check the copy, switch to that slot and empty the overlay. The old slot is not touched
 * (macsys switch goes back to it). progress, if not NULL, is called with 0..100. Returns 0,
 * MACDISK_NOT_SAVED or -1.
 */
int macdisk_promote(void (*progress)(unsigned int percent));

/* Forget everything (for tests). */
void macdisk_forget(void);

#endif
