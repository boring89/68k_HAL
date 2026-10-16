/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_BOOT_MODULE_H
#define KERNEL_BOOT_MODULE_H

#include "hal/boot.h"

/*
 * Find a boot module by name. The boot loader's string for a module is
 * "<file> <name>" (QEMU -initrd "file name") or the line after "module"
 * (GRUB): the module wanted is the one whose last word is name. Returns
 * storage filled in, or NULL if there is none.
 */
const hal_boot_module_t *boot_module_find(const char *name, hal_boot_module_t *storage);

#endif
