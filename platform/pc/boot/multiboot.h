/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_BOOT_MULTIBOOT_H
#define PLATFORM_PC_BOOT_MULTIBOOT_H

#include <stdbool.h>
#include <stdint.h>

/* EAX at kernel entry when a Multiboot loader started the kernel
 * (Multiboot Specification 0.6.96, 3.2 Machine state). */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* EAX at kernel entry when a Multiboot2 loader started the kernel
 * (Multiboot2 Specification 2.0, 3.3 Machine state). */
#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289

/* multiboot_info_t.flags bit 6: mmap_addr / mmap_length are valid
 * (Multiboot Specification 0.6.96, 3.3 Boot information format). */
#define MULTIBOOT_INFO_MEM_MAP (1u << 6)

/* multiboot_info_t.flags bit 3: mods_count / mods_addr are valid. */
#define MULTIBOOT_INFO_MODS (1u << 3)

/* multiboot_info_t.flags bit 12: the framebuffer_* fields are valid. */
#define MULTIBOOT_INFO_FRAMEBUFFER (1u << 12)

/* framebuffer_type values: 1 = direct RGB colour (3.3 Boot information). */
#define MULTIBOOT_FRAMEBUFFER_RGB 1

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

    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;

    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;

    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint8_t color_info[6];

} multiboot_info_t;

/* Field offsets from Multiboot Specification 0.6.96, 3.3 Boot information
 * format. */
_Static_assert(__builtin_offsetof(multiboot_info_t, mmap_addr) == 48,
               "multiboot_info_t must match the Multiboot layout");
_Static_assert(__builtin_offsetof(multiboot_info_t, framebuffer_addr) == 88,
               "multiboot_info_t must match the Multiboot layout");
_Static_assert(__builtin_offsetof(multiboot_info_t, framebuffer_bpp) == 108,
               "multiboot_info_t must match the Multiboot layout");

/*
 * One boot module (Multiboot Specification 0.6.96, 3.3 Boot modules): the
 * file occupies [mod_start, mod_end); string is the address of a
 * NUL-terminated name (the module command line), or 0.
 */
typedef struct
{
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;

} multiboot_module_t;

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

/*
 * Multiboot2 (Multiboot2 Specification 2.0, 3.6 Boot information): the tag
 * list at address is translated into the same multiboot_info_t, memory map
 * and module table that multiboot_init builds from Multiboot 1, so the rest
 * of the kernel does not care which protocol was used. Memory map entries
 * and modules beyond MULTIBOOT2_MAX_MMAP / MULTIBOOT2_MAX_MODULES are
 * dropped. The tag list is never modified. Returns 0, or -1 if the list is
 * malformed (multiboot_init state is then an empty Multiboot 1 structure).
 */
#define MULTIBOOT2_MAX_MMAP 128
#define MULTIBOOT2_MAX_MODULES 8

int multiboot2_init(uint32_t address);

/* The kernel command line (a copy), or NULL if the loader gave none. */
const char *multiboot_cmdline(void);

/* Start and size of the raw Multiboot2 tag list, or 0 for Multiboot 1. */
/* Physical address of the multiboot_info_t that multiboot_get_info returns (a built one for Multiboot2). */
uint32_t multiboot_info_phys(void);

uint32_t multiboot_raw_base(void);
uint32_t multiboot_raw_size(void);

/*
 * Copy of the ACPI RSDP the Multiboot2 loader found (UEFI firmware keeps it
 * outside the BIOS area), or NULL. The copy lives inside the tag list.
 */
const void *multiboot_rsdp(void);

const multiboot_info_t *multiboot_get_info(void);

/*
 * True if the boot loader set up a graphics framebuffer we can use: flags
 * bit 12, direct RGB colour, 32 bpp, a non-empty mode, rows at least as
 * wide as the pixels, and an address below 4 GiB.
 */
bool multiboot_framebuffer_usable(void);

/* True if the loader told us where the colour bits are (Multiboot2); then multiboot_get_info()->color_info is valid. */
bool multiboot_colors_known(void);

/* First memory map entry, or NULL if flags bit 6 is clear or the map is empty. */
const multiboot_mmap_entry_t *multiboot_mmap_first(void);

/* Entry after entry, or NULL at the end of the map. */
const multiboot_mmap_entry_t *multiboot_mmap_next(
    const multiboot_mmap_entry_t *entry);

#endif