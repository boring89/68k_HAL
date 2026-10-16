/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "multiboot.h"

#include <stddef.h>

/* Bytes in an entry after the size field: addr + len + type. */
#define MMAP_ENTRY_MIN_SIZE \
    (sizeof(multiboot_mmap_entry_t) - sizeof(uint32_t))

static const multiboot_info_t *multiboot_info;

/* Memory map bounds; both 0 when the boot loader gave no map. */
static uintptr_t mmap_start;
static uintptr_t mmap_end;

void multiboot_init(uint32_t address)
{
    multiboot_info = (const multiboot_info_t *)(uintptr_t)address;

    mmap_start = 0;
    mmap_end = 0;

    if (multiboot_info->flags & MULTIBOOT_INFO_MEM_MAP)
    {
        mmap_start = multiboot_info->mmap_addr;
        mmap_end = mmap_start + multiboot_info->mmap_length;
    }
}

const multiboot_info_t *multiboot_get_info(void)
{
    return multiboot_info;
}

bool multiboot_framebuffer_usable(void)
{
    const multiboot_info_t *mbi = multiboot_info;

    if (mbi == NULL || !(mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER))
        return false;

    if (mbi->framebuffer_type != MULTIBOOT_FRAMEBUFFER_RGB ||
        mbi->framebuffer_bpp != 32)
        return false;

    return mbi->framebuffer_addr != 0 && (mbi->framebuffer_addr >> 32) == 0 &&
           mbi->framebuffer_width != 0 && mbi->framebuffer_height != 0 &&
           mbi->framebuffer_pitch / 4 >= mbi->framebuffer_width;
}

/* Return the entry at address if it lies completely inside the map. */
static const multiboot_mmap_entry_t *multiboot_mmap_at(uintptr_t address)
{
    if (address < mmap_start || address > mmap_end ||
        mmap_end - address < sizeof(uint32_t))
        return NULL;

    const multiboot_mmap_entry_t *entry =
        (const multiboot_mmap_entry_t *)address;

    /* A short or truncated entry would make us read past the map. */
    if (entry->size < MMAP_ENTRY_MIN_SIZE ||
        mmap_end - address - sizeof(uint32_t) < entry->size)
        return NULL;

    return entry;
}

const multiboot_mmap_entry_t *multiboot_mmap_first(void)
{
    return multiboot_mmap_at(mmap_start);
}

const multiboot_mmap_entry_t *multiboot_mmap_next(
    const multiboot_mmap_entry_t *entry)
{
    uintptr_t next =
        (uintptr_t)entry + sizeof(uint32_t) + entry->size;

    return multiboot_mmap_at(next);
}
