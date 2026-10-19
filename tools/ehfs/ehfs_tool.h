/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TOOLS_EHFS_EHFS_TOOL_H
#define TOOLS_EHFS_EHFS_TOOL_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/block/block.h"
#include "kernel/fs/ehfs/ehfs.h"

/*
 * What the Linux tools (mkfs.ehfs, fsck.ehfs, ehfs-cp) share: a block device
 * on a file. A target that is not a regular file (a disk, /dev/sdX) is
 * refused unless allow_device is set (the tools' --device option): these
 * tools are meant for images first.
 */

typedef struct
{
    block_device_t dev;
    int fd;
} tool_disk_t;

/* Open path as a 512-byte-sector device. Prints why and returns -1 on failure. */
int tool_open(tool_disk_t *disk, const char *path, bool writable, bool allow_device, const char *program);

/* Flush and close. */
void tool_close(tool_disk_t *disk);

/* A message for an EHFS_E... code. */
const char *tool_error(int code);

/* Nanoseconds since 1970 now. */
uint64_t tool_now(void);

#endif
