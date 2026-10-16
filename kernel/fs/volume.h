/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_VOLUME_H
#define KERNEL_FS_VOLUME_H

#include "kernel/fs/hfs/hfs.h"

/*
 * The mounted volume the shell's file commands use. There is only one.
 */

/*
 * Look through the registered block devices, in order, for an HFS volume:
 * the first Apple_HFS partition of an Apple Partition Map, or else a bare
 * HFS volume (no partition map) starting at block 0. Mounts the first one
 * found, replacing the current volume. Returns 0, or -1 if there is none.
 */
int volume_mount_any(void);

/* The mounted volume, or NULL. */
hfs_volume_t *volume_get(void);

/* Forget the mounted volume. */
void volume_unmount(void);

#endif
