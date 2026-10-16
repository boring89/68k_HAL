/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_VFS_HFSFS_H
#define KERNEL_VFS_HFSFS_H

#include "vfs.h"

/*
 * The HFS volume of the Mac disk (kernel/fs/volume.h) as a read-only
 * file system, for mounting at /hfs. Folders are separated by '/'
 * as everywhere in the file tree (HFS itself uses ':'; a '/' inside an HFS
 * name shows up as ':'). Only the data fork of a file can be read, and
 * file bytes are unchanged, so Mac text files end their lines with CR.
 * Without a mounted volume the file system is empty.
 */
vfs_fs_t *hfsfs_create(void);

#endif
