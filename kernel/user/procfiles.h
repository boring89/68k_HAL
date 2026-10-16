/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_USER_PROCFILES_H
#define KERNEL_USER_PROCFILES_H

#include "kernel/vfs/vfs.h"

/*
 * The files of /proc (Linux formats): version, uptime ("seconds.hundredths 0.00"),
 * meminfo (MemTotal, MemFree in kB, and the kernel heap), cpuinfo (one block per processor).
 * NULL if out of memory.
 */
vfs_fs_t *procfiles_create(void);

#endif
