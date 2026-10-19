/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MAC_FS_EHFS_H
#define KERNEL_MAC_FS_EHFS_H

#include <stdint.h>

#include "emu/mac/fsm.h"
#include "kernel/fs/ehfs/ehfs.h"

/*
 * An eHFS volume (docs/ehfs.md) as a volume of the Mac's desktop through the
 * external file system (emu/mac/fsm.c, docs/fsm.md): read and write. The
 * Mac's IDs are the eHFS CNIDs (decision E5 keeps them below 2^32). Names: an
 * eHFS name the Mac cannot show as it is (not ASCII, longer than 31, a
 * control character) is shown with "#<CNID>" at its end, as on the HAL
 * volume; a name the Mac gives must be ASCII for now (docs/fsm.md 4.4).
 */

/* Nanoseconds since 1970 (UTC), for the times of what the Mac changes. */
typedef uint64_t (*mac_fs_ehfs_clock_t)(void);

typedef struct
{
    mac_fs_ops_t ops;               /* give &volume->ops to mac_fsm_attach */
    ehfs_t *fs;
    mac_fs_ehfs_clock_t clock;
    char name[28];                  /* the Mac's name of the volume */
} mac_fs_ehfs_t;

/* Set up volume for the mounted fs. */
void mac_fs_ehfs_init(mac_fs_ehfs_t *volume, ehfs_t *fs, mac_fs_ehfs_clock_t clock);

#endif
