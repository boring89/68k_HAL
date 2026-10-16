/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_M68K_GUEST_H
#define KERNEL_M68K_GUEST_H

#include <stdint.h>

/*
 * The 68k virtual machine (emu/m68k) as seen by the kernel: a guest RAM
 * taken from the PMM and mapped at a fixed virtual address, running a
 * 68030. The RAM is 8 MiB unless m68k_guest_set_size changes it (System 7
 * needs at least that much).
 */

#define M68K_GUEST_RAM_BASE 0x10000000u /* kernel virtual address */
#define M68K_GUEST_RAM_DEFAULT_SIZE (8u * 1024 * 1024)
/* 256 MiB: the range 0x10000000-0x1FFFFFFF is reserved for the guest RAM. */
#define M68K_GUEST_RAM_MAX_SIZE (256u * 1024 * 1024)

/*
 * Set the guest RAM size in bytes: a multiple of 4 KiB, at most
 * M68K_GUEST_RAM_MAX_SIZE. If RAM of another size is already allocated it is
 * freed, so call m68k_guest_init afterwards (the VM is detached until then).
 * Returns 0, or -1 for a bad size.
 */
int m68k_guest_set_size(uint32_t size);

/* The size the next m68k_guest_init (or the last one) uses, in bytes. */
uint32_t m68k_guest_ram_size(void);

/*
 * Allocate and map the guest RAM (once) and attach the VM to it. Calling it
 * again just clears the RAM and re-attaches. Returns 0, or -1 if memory or a
 * mapping is not available.
 */
int m68k_guest_init(void);

#endif
