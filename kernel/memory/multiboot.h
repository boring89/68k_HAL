/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MEMORY_MULTIBOOT_H
#define KERNEL_MEMORY_MULTIBOOT_H

#include <stdint.h>

/* multiboot_info_t.flags bit 6: mmap_addr / mmap_length are valid
 * (Multiboot Specification 0.6.96, 3.3 Boot information format). */
#define MULTIBOOT_INFO_MEM_MAP (1u << 6)

/* Memory map entry types. 1 is defined by the 0.6.96 spec; 3-5 follow
 * GRUB's multiboot.h (same values as the BIOS E820 types). */
#define MULTIBOOT_MEMORY_AVAILABLE 1
#define MULTIBOOT_MEMORY_RESERVED 2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS 4
#define MULTIBOOT_MEMORY_BADRAM 5

typedef struct
{
    uint32_t flags;

    uint32_t mem_lower;
    uint32_t mem_upper;

    uint32_t boot_device;

    uint32_t cmdline;

    uint32_t mods_count;
    uint32_t mods_addr;

    uint32_t syms[4];

    uint32_t mmap_length;
    uint32_t mmap_addr;

} multiboot_info_t;

/*
 * One memory map entry as laid out by the boot loader: fields are
 * contiguous, so the struct must be packed (x86_64 would otherwise pad
 * after size). size does not count itself: the next entry starts at
 * (entry address + size + 4), and may be larger than this struct.
 */
typedef struct __attribute__((packed))
{
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;

} multiboot_mmap_entry_t;

_Static_assert(sizeof(multiboot_mmap_entry_t) == 24,
               "multiboot_mmap_entry_t must match the Multiboot layout");

void multiboot_init(uint32_t address);

const multiboot_info_t *multiboot_get_info(void);

/* First memory map entry, or NULL if flags bit 6 is clear or the map is empty. */
const multiboot_mmap_entry_t *multiboot_mmap_first(void);

/* Entry after entry, or NULL at the end of the map. */
const multiboot_mmap_entry_t *multiboot_mmap_next(
    const multiboot_mmap_entry_t *entry);

#endif