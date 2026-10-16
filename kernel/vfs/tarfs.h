/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_VFS_TARFS_H
#define KERNEL_VFS_TARFS_H

#include "vfs.h"

/*
 * A read-only file system on a tar archive in memory (the "initrd":
 * programs and data packed into one boot module). Understands ustar
 * (POSIX.1-1988) archives: regular files and directories, names up to
 * 100 characters plus the 155-character prefix field. Directories that
 * only appear as part of a file's path ("bin/hello") exist too.
 */

/*
 * Index the archive of size bytes at data, which must stay valid as
 * long as the file system is used. Returns NULL if it is not a tar
 * archive or memory is short.
 */
vfs_fs_t *tarfs_create(const void *data, size_t size);

void tarfs_destroy(vfs_fs_t *fs);

#endif
