/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_BLOCK_RAMDISK_H
#define KERNEL_BLOCK_RAMDISK_H

#include <stddef.h>

/* Block size of a RAM disk: the classic 512-byte sector. */
#define RAMDISK_BLOCK_SIZE 512

#define RAMDISK_MAX 4

/*
 * Register size / 512 blocks (a partial last block is ignored) of the memory
 * at data as the block device name. The memory is only read and must stay
 * valid; name must too. Returns 0, or -1 if the pool is full, the memory
 * holds less than one block or block_register fails.
 */
int ramdisk_register(const char *name, const void *data, size_t size);

/*
 * Register every boot module (hal/boot.h) as ram0, ram1, ... Returns how
 * many were registered.
 */
unsigned int ramdisk_register_boot_modules(void);

/* Forget the RAM disks (for tests; call block_reset too). */
void ramdisk_reset(void);

#endif
