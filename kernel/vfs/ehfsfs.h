/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_VFS_EHFSFS_H
#define KERNEL_VFS_EHFSFS_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/block/block.h"
#include "kernel/fs/ehfs/ehfs.h"
#include "vfs.h"

/*
 * The eHFS volume (docs/ehfs.md) in the file tree, mounted at /ehfs: files
 * and folders can be read and written there. There is one volume at a time.
 * While the Mac uses it (decision F4), the file tree only reads it.
 */

/* Nanoseconds since 1970 (UTC), for the times of what is written. */
typedef uint64_t (*ehfsfs_clock_fn)(void);

/*
 * Look through the registered block devices, in order, for an eHFS volume:
 * a bare one at sector 0, or else one at the start of a GPT partition. Mounts
 * the first one found (read-only if the device cannot be written), replacing
 * the current volume. Returns 0, or -1 if there is none.
 */
int ehfsfs_mount_any(void);

/* Mount the volume at start_sector of device, replacing the current one. Returns 0 or an EHFS_E... code. */
int ehfsfs_mount(block_device_t *device, uint64_t start_sector, bool read_only);

/* Mark the volume cleanly unmounted and forget it (before a power off; files still open fail with EIO). */
void ehfsfs_unmount(void);

/* The mounted volume, or NULL. */
ehfs_t *ehfsfs_volume(void);

/* While the Mac uses the volume: everything that would change it fails with EROFS. */
void ehfsfs_set_mac_running(bool running);

/* The file system for vfs_mount: an empty root while no volume is mounted. */
vfs_fs_t *ehfsfs_create(ehfsfs_clock_fn clock);

#endif
