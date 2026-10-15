/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_M68K_GUEST_H
#define KERNEL_M68K_GUEST_H

#include <stdint.h>

/*
 * The 68k virtual machine (emu/m68k) as seen by the kernel: a 4 MiB guest
 * RAM taken from the PMM and mapped at a fixed virtual address, running a
 * 68030.
 */

#define M68K_GUEST_RAM_BASE 0x10000000u /* kernel virtual address */
#define M68K_GUEST_RAM_SIZE (4u * 1024 * 1024)

/*
 * Allocate and map the guest RAM (once) and attach the VM to it. Calling it
 * again just clears the RAM and re-attaches. Returns 0, or -1 if memory or a
 * mapping is not available.
 */
int m68k_guest_init(void);

#endif
