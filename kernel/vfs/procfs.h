/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_VFS_PROCFS_H
#define KERNEL_VFS_PROCFS_H

#include "vfs.h"

/*
 * A file system of read-only text files whose contents are made when
 * they are opened (like /proc on Linux): the kernel reports its state
 * as text that programs read with the ordinary file calls. One flat
 * directory.
 */

#define PROCFS_MAX_FILES 16
#define PROCFS_MAX_SIZE 4096

/* Write the text into buffer (size bytes) and return its length (at most size). */
typedef size_t (*procfs_generator_t)(char *buffer, size_t size);

/* NULL if out of memory. */
vfs_fs_t *procfs_create(void);

/* Add a file called name (no slash). Returns 0, or -1 if the name is bad or the table is full. */
int procfs_add(vfs_fs_t *fs, const char *name, procfs_generator_t generator);

void procfs_destroy(vfs_fs_t *fs);

#endif
