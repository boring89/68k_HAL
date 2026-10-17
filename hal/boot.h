/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_BOOT_H
#define HAL_BOOT_H

#include <stdint.h>

/*
 * What the boot loader or firmware tells the kernel about the machine.
 * Implemented once per platform (platform/<platform>/boot/hal_boot.c).
 */

/*
 * Memory region types: the ACPI address range types (ACPI Specification,
 * "System Address Map Interfaces"), which BIOS E820, UEFI and Multiboot
 * all report. Any other value must be treated as reserved.
 */
#define HAL_MEMORY_AVAILABLE        1 /* usable RAM */
#define HAL_MEMORY_RESERVED         2
#define HAL_MEMORY_ACPI_RECLAIMABLE 3 /* usable once the ACPI tables are read */
#define HAL_MEMORY_ACPI_NVS         4
#define HAL_MEMORY_UNUSABLE         5 /* RAM with errors */

typedef struct
{
    uint64_t base;
    uint64_t length;
    uint32_t type; /* HAL_MEMORY_* */
} hal_memory_region_t;

/*
 * Region index of the physical memory map, in the order the boot loader
 * gave them; regions may overlap. Returns 0, or -1 if index is past the
 * end (or there is no map) or region is NULL.
 */
int hal_boot_memory_region(unsigned int index, hal_memory_region_t *region);

/*
 * Region index of the memory that holds the boot information itself;
 * it must not be handed out as free memory. type is HAL_MEMORY_RESERVED.
 * Returns 0, or -1 if index is past the end or region is NULL.
 */
int hal_boot_reserved_region(unsigned int index, hal_memory_region_t *region);

/*
 * Files the boot loader loaded next to the kernel (GRUB "module" lines,
 * QEMU -initrd). The memory they occupy is reported by
 * hal_boot_reserved_region, so it is never handed out as free memory, and is
 * mapped read-only once paging is on.
 */
typedef struct
{
    uint64_t base;
    uint64_t length;
    const char *name; /* NUL-terminated, "" if the loader gave none */
} hal_boot_module_t;

/* Number of boot modules (0 if the loader gave none). */
unsigned int hal_boot_module_count(void);

/* Boot module index. Returns 0, or -1 if index is out of range or module is NULL. */
int hal_boot_module(unsigned int index, hal_boot_module_t *module);

/*
 * The kernel command line the boot loader passed ("multiboot /boot/kernel.elf noapic" in GRUB,
 * QEMU -append), or NULL if there is none. Options are words separated by spaces.
 */
const char *hal_boot_cmdline(void);

#endif
