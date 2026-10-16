/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_VFS_RAMFS_H
#define KERNEL_VFS_RAMFS_H

#include "vfs.h"

/*
 * A read/write file system that lives in the kernel heap: the /tmp of
 * the user programs. Files grow as they are written; nothing survives
 * a reboot.
 */

/* Create an empty ramfs. Free it with ramfs_destroy. NULL if out of memory. */
vfs_fs_t *ramfs_create(void);

/* Free the file system and everything in it. No file of it may be open. */
void ramfs_destroy(vfs_fs_t *fs);

#endif
