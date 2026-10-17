/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "multiboot.h"

#include "kernel/lib/k_memory.h"

#include <stddef.h>

/* Bytes in an entry after the size field: addr + len + type. */
#define MMAP_ENTRY_MIN_SIZE \
    (sizeof(multiboot_mmap_entry_t) - sizeof(uint32_t))

static const multiboot_info_t *multiboot_info;

/*
 * The kernel command line, copied: the loader's memory is not mapped once paging is on, and
 * the options are read late (interrupt controller set-up).
 */
#define CMDLINE_MAX 256
static char cmdline_copy[CMDLINE_MAX];
static bool cmdline_present;

static void copy_cmdline(const char *source, uint32_t limit)
{
    uint32_t i = 0;

    for (; i < CMDLINE_MAX - 1 && i < limit && source[i] != '\0'; i++)
        cmdline_copy[i] = source[i];

    cmdline_copy[i] = '\0';
    cmdline_present = true;
}

const char *multiboot_cmdline(void)
{
    return cmdline_present ? cmdline_copy : NULL;
}

/*
 * True when color_info came from a Multiboot2 tag. In a Multiboot 1 info structure the colour
 * fields sit where GRUB's compiler put them (offset 112, not the 110 of the specification), so
 * they are not trusted there.
 */
static bool colors_known;

/* Multiboot2 translation (multiboot2_init). */
static multiboot_info_t synth_info;
static multiboot_mmap_entry_t synth_mmap[MULTIBOOT2_MAX_MMAP];
static multiboot_module_t synth_modules[MULTIBOOT2_MAX_MODULES];
static uint32_t raw_base;
static uint32_t raw_size;
static const void *rsdp_copy;

/* Memory map bounds; both 0 when the boot loader gave no map. */
static uintptr_t mmap_start;
static uintptr_t mmap_end;

void multiboot_init(uint32_t address)
{
    multiboot_info = (const multiboot_info_t *)(uintptr_t)address;

    raw_base = 0;
    raw_size = 0;
    rsdp_copy = NULL;
    mmap_start = 0;
    mmap_end = 0;
    cmdline_present = false;
    colors_known = false;

    /* flags bit 2 (Multiboot 0.6.96 3.3): the cmdline field points at a string. */
    if ((multiboot_info->flags & (1u << 2)) && multiboot_info->cmdline != 0)
        copy_cmdline((const char *)(uintptr_t)multiboot_info->cmdline, CMDLINE_MAX);

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

    /*
     * Everything that draws on the framebuffer assumes 0x00RRGGBB (blue in the low byte: UEFI's
     * "BGRX"). A loader that says otherwise (red in the low byte) would get swapped colours.
     */
    static const uint8_t bgrx[6] = {16, 8, 8, 8, 0, 8};
    int same = 1;

    for (int i = 0; i < 6; i++)
        same &= mbi->color_info[i] == bgrx[i];

    if (colors_known && !same)
        return false;

    return mbi->framebuffer_addr != 0 && (mbi->framebuffer_addr >> 32) == 0 &&
           mbi->framebuffer_width != 0 && mbi->framebuffer_height != 0 &&
           mbi->framebuffer_pitch / 4 >= mbi->framebuffer_width;
}

bool multiboot_colors_known(void)
{
    return colors_known;
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

/*
 * Multiboot2 tag types and layouts (Multiboot2 Specification 2.0, 3.6).
 * Every tag starts with {type, size} and the next tag begins at the next
 * 8-byte boundary.
 */
#define MB2_TAG_END 0
#define MB2_TAG_CMDLINE 1
#define MB2_TAG_MODULE 3
#define MB2_TAG_MMAP 6
#define MB2_TAG_FRAMEBUFFER 8
#define MB2_TAG_ACPI_OLD 14
#define MB2_TAG_ACPI_NEW 15

typedef struct
{
    uint32_t type;
    uint32_t size;
} mb2_tag_t;

typedef struct
{
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t reserved;
} mb2_mmap_entry_t;

int multiboot2_init(uint32_t address)
{
    const uint8_t *base = (const uint8_t *)(uintptr_t)address;
    uint32_t total = ((const uint32_t *)base)[0];

    k_memset(&synth_info, 0, sizeof(synth_info));

    raw_base = address;
    raw_size = total;
    rsdp_copy = NULL;
    cmdline_present = false;
    colors_known = false;
    mmap_start = 0;
    mmap_end = 0;
    multiboot_info = &synth_info;

    if (total < 16)
        return -1;

    unsigned int mmap_count = 0;
    unsigned int module_count = 0;

    for (uint32_t offset = 8; offset + sizeof(mb2_tag_t) <= total;)
    {
        const mb2_tag_t *tag = (const mb2_tag_t *)(base + offset);

        if (tag->type == MB2_TAG_END)
            break;

        if (tag->size < sizeof(*tag) || tag->size > total - offset)
            return -1;

        const uint8_t *body = (const uint8_t *)tag + sizeof(*tag);
        uint32_t body_size = tag->size - sizeof(*tag);

        switch (tag->type)
        {
        case MB2_TAG_CMDLINE:
            /* A NUL-terminated string in the tag list itself. */
            copy_cmdline((const char *)body, body_size);
            break;
        case MB2_TAG_MMAP:
        {
            /* entry_size, entry_version, then the entries. */
            if (body_size < 8)
                return -1;

            uint32_t entry_size = ((const uint32_t *)body)[0];

            if (entry_size < sizeof(mb2_mmap_entry_t))
                return -1;

            for (uint32_t pos = 8; pos + entry_size <= body_size &&
                                   mmap_count < MULTIBOOT2_MAX_MMAP;
                 pos += entry_size)
            {
                const mb2_mmap_entry_t *entry =
                    (const mb2_mmap_entry_t *)(body + pos);

                synth_mmap[mmap_count].size = MMAP_ENTRY_MIN_SIZE;
                synth_mmap[mmap_count].addr = entry->addr;
                synth_mmap[mmap_count].len = entry->len;
                synth_mmap[mmap_count].type = entry->type;
                mmap_count++;
            }
            break;
        }
        case MB2_TAG_MODULE:
            /* mod_start, mod_end, then the NUL-terminated command line. */
            if (body_size >= 8 && module_count < MULTIBOOT2_MAX_MODULES)
            {
                synth_modules[module_count].mod_start = ((const uint32_t *)body)[0];
                synth_modules[module_count].mod_end = ((const uint32_t *)body)[1];
                synth_modules[module_count].string =
                    body_size > 8 ? (uint32_t)(uintptr_t)(body + 8) : 0;
                synth_modules[module_count].reserved = 0;
                module_count++;
            }
            break;
        case MB2_TAG_FRAMEBUFFER:
            /* addr(8) pitch(4) width(4) height(4) bpp(1) type(1) reserved(2) */
            if (body_size >= 22)
            {
                synth_info.flags |= MULTIBOOT_INFO_FRAMEBUFFER;
                synth_info.framebuffer_addr = *(const uint64_t *)body;
                synth_info.framebuffer_pitch = ((const uint32_t *)body)[2];
                synth_info.framebuffer_width = ((const uint32_t *)body)[3];
                synth_info.framebuffer_height = ((const uint32_t *)body)[4];
                synth_info.framebuffer_bpp = body[20];
                synth_info.framebuffer_type = body[21];

                /* Direct colour: where the red, green and blue bits sit (position, size; bytes 24-29). */
                if (body[21] == MULTIBOOT_FRAMEBUFFER_RGB && body_size >= 30)
                {
                    for (int i = 0; i < 6; i++)
                        synth_info.color_info[i] = body[24 + i];

                    colors_known = true;
                }
            }
            break;
        case MB2_TAG_ACPI_OLD:
        case MB2_TAG_ACPI_NEW:
            /* A copy of the RSDP; the newer tag wins. */
            if (body_size >= 20 &&
                (rsdp_copy == NULL || tag->type == MB2_TAG_ACPI_NEW))
                rsdp_copy = body;
            break;
        default:
            break;
        }

        offset += (tag->size + 7u) & ~7u;
    }

    if (mmap_count > 0)
    {
        synth_info.flags |= MULTIBOOT_INFO_MEM_MAP;
        synth_info.mmap_addr = (uint32_t)(uintptr_t)synth_mmap;
        synth_info.mmap_length =
            mmap_count * (uint32_t)sizeof(multiboot_mmap_entry_t);
        mmap_start = (uintptr_t)synth_mmap;
        mmap_end = mmap_start + synth_info.mmap_length;
    }

    if (module_count > 0)
    {
        synth_info.flags |= MULTIBOOT_INFO_MODS;
        synth_info.mods_count = module_count;
        synth_info.mods_addr = (uint32_t)(uintptr_t)synth_modules;
    }

    return 0;
}

uint32_t multiboot_raw_base(void)
{
    return raw_base;
}

uint32_t multiboot_raw_size(void)
{
    return raw_size;
}

const void *multiboot_rsdp(void)
{
    return rsdp_copy;
}
