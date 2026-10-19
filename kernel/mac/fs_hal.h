/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MAC_FS_HAL_H
#define KERNEL_MAC_FS_HAL_H

#include "emu/mac/fsm.h"

/*
 * The HAL's file tree (kernel/vfs) as a read-only Mac volume called "HAL"
 * (docs/fsm.md): the external file system of emu/mac/fsm.c through
 * emu/mac/fs_paths.c. /dev and /proc are left out. NULL if there is no
 * memory for it.
 */
const mac_fs_ops_t *mac_fs_hal(bool leave_out_ehfs);

#endif
