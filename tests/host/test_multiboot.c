/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* MAP_32BIT needs _GNU_SOURCE under -std=c11. */
#define _GNU_SOURCE

#include "test.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "platform/pc/boot/multiboot.h"
#include "hal/boot.h"

/*
 * multiboot_info_t stores 32-bit addresses, so the fake boot data must
 * live below 4 GiB even on a 64-bit host: map it with MAP_32BIT.
 */
#define AREA_SIZE 4096

static uint8_t *area;
static multiboot_info_t *mbi;
static uint8_t *table;

static uint32_t addr32(const void *ptr)
{
    return (uint32_t)(uintptr_t)ptr;
}

static void reset(void)
{
    memset(area, 0, AREA_SIZE);
    mbi = (multiboot_info_t *)area;
    table = area + 256;
}

/* Append an entry with the given size field; returns the next write position. */
static uint8_t *put_entry(uint8_t *pos, uint32_t size,
                          uint64_t addr, uint64_t len, uint32_t type)
{
    multiboot_mmap_entry_t entry = {size, addr, len, type};

    memcpy(pos, &entry, sizeof(entry));
    return pos + sizeof(uint32_t) + size;
}

static void boot(uint32_t flags, const uint8_t *end)
{
    mbi->flags = flags;
    mbi->mmap_addr = addr32(table);
    mbi->mmap_length = (uint32_t)(end - table);
    multiboot_init(addr32(mbi));
}

static void test_entry_layout(void)
{
    TEST_ASSERT_EQ_INT(24, sizeof(multiboot_mmap_entry_t));
    TEST_ASSERT_EQ_INT(4, offsetof(multiboot_mmap_entry_t, addr));
    TEST_ASSERT_EQ_INT(12, offsetof(multiboot_mmap_entry_t, len));
    TEST_ASSERT_EQ_INT(20, offsetof(multiboot_mmap_entry_t, type));
}

static void test_no_map_flag(void)
{
    reset();

    uint8_t *end = put_entry(table, 20, 0, 0x9FC00, 1);

    boot(0, end);

    TEST_ASSERT(multiboot_mmap_first() == NULL);
}

static void test_empty_map(void)
{
    reset();
    boot(MULTIBOOT_INFO_MEM_MAP, table);

    TEST_ASSERT(multiboot_mmap_first() == NULL);
}

static void test_walk_entries(void)
{
    reset();

    uint8_t *end = table;
    end = put_entry(end, 20, 0x0, 0x9FC00, 1);
    end = put_entry(end, 20, 0x9FC00, 0x400, 2);
    end = put_entry(end, 20, 0xFFFC0000ULL, 0x40000, 2);

    boot(MULTIBOOT_INFO_MEM_MAP, end);

    const multiboot_mmap_entry_t *e = multiboot_mmap_first();
    TEST_ASSERT(e != NULL);
    TEST_ASSERT_EQ_INT(0x9FC00, e->len);
    TEST_ASSERT_EQ_INT(1, e->type);

    e = multiboot_mmap_next(e);
    TEST_ASSERT(e != NULL);
    TEST_ASSERT_EQ_INT(0x9FC00, e->addr);

    e = multiboot_mmap_next(e);
    TEST_ASSERT(e != NULL);
    TEST_ASSERT_EQ_INT(0xFFFC0000LL, e->addr);

    TEST_ASSERT(multiboot_mmap_next(e) == NULL);
}

static void test_larger_entry_uses_size_field(void)
{
    reset();

    /* size = 28: 8 extra bytes the iterator must skip, not sizeof(). */
    uint8_t *end = table;
    end = put_entry(end, 28, 0x0, 0x1000, 1);
    end = put_entry(end, 20, 0x100000, 0x2000, 1);

    boot(MULTIBOOT_INFO_MEM_MAP, end);

    const multiboot_mmap_entry_t *e = multiboot_mmap_first();
    TEST_ASSERT(e != NULL);

    e = multiboot_mmap_next(e);
    TEST_ASSERT(e != NULL);
    TEST_ASSERT_EQ_INT(0x100000, e->addr);
    TEST_ASSERT_EQ_INT(0x2000, e->len);

    TEST_ASSERT(multiboot_mmap_next(e) == NULL);
}

static void test_truncated_entry_rejected(void)
{
    reset();

    uint8_t *end = table;
    end = put_entry(end, 20, 0x0, 0x1000, 1);
    end = put_entry(end, 20, 0x100000, 0x2000, 1);

    /* mmap_length cuts the second entry in half. */
    boot(MULTIBOOT_INFO_MEM_MAP, end - 10);

    const multiboot_mmap_entry_t *e = multiboot_mmap_first();
    TEST_ASSERT(e != NULL);
    TEST_ASSERT(multiboot_mmap_next(e) == NULL);
}

static void test_short_size_rejected(void)
{
    reset();

    /* size smaller than addr + len + type would read past the entry. */
    uint8_t *end = put_entry(table, 8, 0x0, 0x1000, 1);

    boot(MULTIBOOT_INFO_MEM_MAP, end + 16);

    TEST_ASSERT(multiboot_mmap_first() == NULL);
}

static void test_hal_memory_regions(void)
{
    reset();

    uint8_t *end = put_entry(table, 20, 0x0, 0x9FC00, 1);
    end = put_entry(end, 20, 0x9FC00, 0x400, 2);
    end = put_entry(end, 20, 0x100000, 0x7EE0000, 1);
    /* Unknown types pass through unchanged. */
    end = put_entry(end, 20, 0xFFFC0000, 0x40000, 12);
    boot(MULTIBOOT_INFO_MEM_MAP, end);

    hal_memory_region_t region;

    TEST_ASSERT_EQ_INT(0, hal_boot_memory_region(0, &region));
    TEST_ASSERT_EQ_INT(0x0, region.base);
    TEST_ASSERT_EQ_INT(0x9FC00, region.length);
    TEST_ASSERT_EQ_INT(HAL_MEMORY_AVAILABLE, region.type);

    TEST_ASSERT_EQ_INT(0, hal_boot_memory_region(1, &region));
    TEST_ASSERT_EQ_INT(HAL_MEMORY_RESERVED, region.type);

    TEST_ASSERT_EQ_INT(0, hal_boot_memory_region(2, &region));
    TEST_ASSERT_EQ_INT(0x100000, region.base);
    TEST_ASSERT_EQ_INT(0x7EE0000, region.length);

    TEST_ASSERT_EQ_INT(0, hal_boot_memory_region(3, &region));
    TEST_ASSERT_EQ_INT(12, region.type);

    TEST_ASSERT_EQ_INT(-1, hal_boot_memory_region(4, &region));
    TEST_ASSERT_EQ_INT(-1, hal_boot_memory_region(0, NULL));
}

static void test_hal_memory_regions_without_map(void)
{
    reset();

    uint8_t *end = put_entry(table, 20, 0, 0x9FC00, 1);
    boot(0, end);

    hal_memory_region_t region;

    TEST_ASSERT_EQ_INT(-1, hal_boot_memory_region(0, &region));
}

static void test_hal_reserved_regions(void)
{
    reset();

    uint8_t *end = put_entry(table, 20, 0, 0x9FC00, 1);
    boot(MULTIBOOT_INFO_MEM_MAP, end);

    hal_memory_region_t region;

    TEST_ASSERT_EQ_INT(0, hal_boot_reserved_region(0, &region));
    TEST_ASSERT_EQ_INT(addr32(mbi), region.base);
    TEST_ASSERT_EQ_INT(sizeof(multiboot_info_t), region.length);
    TEST_ASSERT_EQ_INT(HAL_MEMORY_RESERVED, region.type);

    TEST_ASSERT_EQ_INT(0, hal_boot_reserved_region(1, &region));
    TEST_ASSERT_EQ_INT(addr32(table), region.base);
    TEST_ASSERT_EQ_INT(24, region.length);

    TEST_ASSERT_EQ_INT(-1, hal_boot_reserved_region(2, &region));
    TEST_ASSERT_EQ_INT(-1, hal_boot_reserved_region(0, NULL));
}

/* Without flags bit 6 the memory map is not boot data to keep. */
static void test_hal_reserved_regions_without_map(void)
{
    reset();
    boot(0, table);

    hal_memory_region_t region;

    TEST_ASSERT_EQ_INT(0, hal_boot_reserved_region(0, &region));
    TEST_ASSERT_EQ_INT(-1, hal_boot_reserved_region(1, &region));
}

/* Modules: table, then contents and name of each, after the info and map. */
static void test_hal_modules(void)
{
    reset();

    uint8_t *end = put_entry(table, 20, 0, 0x9FC00, 1);
    multiboot_module_t *mods = (multiboot_module_t *)(area + 1024);
    char *names = (char *)(area + 1536);

    memcpy(names, "kernel.bin boot", 16);
    mods[0] = (multiboot_module_t){0x200000, 0x200400, addr32(names), 0};
    mods[1] = (multiboot_module_t){0x300000, 0x300000, 0, 0};
    boot(MULTIBOOT_INFO_MEM_MAP | MULTIBOOT_INFO_MODS, end);
    mbi->mods_count = 2;
    mbi->mods_addr = addr32(mods);
    multiboot_init(addr32(mbi));

    hal_boot_module_t module;

    TEST_ASSERT_EQ_INT(2, hal_boot_module_count());
    TEST_ASSERT_EQ_INT(0, hal_boot_module(0, &module));
    TEST_ASSERT_EQ_INT(0x200000, module.base);
    TEST_ASSERT_EQ_INT(0x400, module.length);
    TEST_ASSERT_EQ_INT(0, strcmp(module.name, "kernel.bin boot"));
    TEST_ASSERT_EQ_INT(0, hal_boot_module(1, &module));
    TEST_ASSERT_EQ_INT(0, module.length);
    TEST_ASSERT_EQ_INT(0, strcmp(module.name, ""));
    TEST_ASSERT_EQ_INT(-1, hal_boot_module(2, &module));
    TEST_ASSERT_EQ_INT(-1, hal_boot_module(0, NULL));

    hal_memory_region_t region;

    TEST_ASSERT_EQ_INT(0, hal_boot_reserved_region(2, &region));
    TEST_ASSERT_EQ_INT(addr32(mods), region.base);
    TEST_ASSERT_EQ_INT(2 * sizeof(multiboot_module_t), region.length);
    TEST_ASSERT_EQ_INT(0, hal_boot_reserved_region(3, &region));
    TEST_ASSERT_EQ_INT(0x200000, region.base);
    TEST_ASSERT_EQ_INT(0x400, region.length);
    TEST_ASSERT_EQ_INT(0, hal_boot_reserved_region(4, &region));
    TEST_ASSERT_EQ_INT(addr32(names), region.base);
    TEST_ASSERT_EQ_INT(16, region.length);
    TEST_ASSERT_EQ_INT(0, hal_boot_reserved_region(5, &region));
    TEST_ASSERT_EQ_INT(0x300000, region.base);
    TEST_ASSERT_EQ_INT(0, hal_boot_reserved_region(6, &region));
    TEST_ASSERT_EQ_INT(0, region.length);
    TEST_ASSERT_EQ_INT(-1, hal_boot_reserved_region(7, &region));
}

/* Without flags bit 3 mods_count is not valid. */
static void test_hal_modules_flag_clear(void)
{
    reset();
    boot(0, table);
    mbi->mods_count = 3;

    hal_boot_module_t module;

    TEST_ASSERT_EQ_INT(0, hal_boot_module_count());
    TEST_ASSERT_EQ_INT(-1, hal_boot_module(0, &module));
}

static void test_framebuffer_usable(void)
{
    reset();
    mbi->flags = MULTIBOOT_INFO_FRAMEBUFFER;
    mbi->framebuffer_addr = 0xFD000000u;
    mbi->framebuffer_pitch = 2560;
    mbi->framebuffer_width = 640;
    mbi->framebuffer_height = 480;
    mbi->framebuffer_bpp = 32;
    mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_RGB;
    multiboot_init(addr32(mbi));
    TEST_ASSERT(multiboot_framebuffer_usable());

    /* Each field that makes the mode unusable. */
    mbi->framebuffer_bpp = 24;
    TEST_ASSERT(!multiboot_framebuffer_usable());
    mbi->framebuffer_bpp = 32;
    mbi->framebuffer_type = 2; /* EGA text */
    TEST_ASSERT(!multiboot_framebuffer_usable());
    mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_RGB;
    mbi->framebuffer_pitch = 2559;
    TEST_ASSERT(!multiboot_framebuffer_usable());
    mbi->framebuffer_pitch = 2560;
    mbi->framebuffer_addr = 0x100000000ull;
    TEST_ASSERT(!multiboot_framebuffer_usable());
    mbi->framebuffer_addr = 0xFD000000u;
    mbi->framebuffer_height = 0;
    TEST_ASSERT(!multiboot_framebuffer_usable());
    mbi->framebuffer_height = 480;
    TEST_ASSERT(multiboot_framebuffer_usable());

    /* A Multiboot 1 loader's colour bytes are not trusted (GRUB puts them at another offset). */
    static const uint8_t rgbx[6] = {0, 8, 8, 8, 16, 8};

    memcpy(mbi->color_info, rgbx, 6);
    TEST_ASSERT(multiboot_framebuffer_usable());

    /* Text mode: the loader leaves flags bit 12 clear. */
    mbi->flags = 0;
    TEST_ASSERT(!multiboot_framebuffer_usable());
}

/* Append one Multiboot2 tag (8-byte aligned) at pos; returns the next position. */
static uint8_t *put_tag(uint8_t *pos, uint32_t type, const void *body, uint32_t body_size)
{
    uint32_t header[2] = {type, 8 + body_size};

    memcpy(pos, header, 8);
    if (body_size != 0)
        memcpy(pos + 8, body, body_size);
    return pos + ((8 + body_size + 7) & ~7u);
}

static void test_multiboot2_tags(void)
{
    reset();

    uint8_t *pos = area + 8;
    uint8_t mmap_body[8 + 2 * 24] = {0};
    uint32_t entry_size = 24;
    uint64_t e0[3] = {0x0, 0x9FC00, 1 | ((uint64_t)0 << 32)};
    uint64_t e1[3] = {0x100000, 0x7EE00000, 1};

    memcpy(mmap_body, &entry_size, 4);
    memcpy(mmap_body + 8, e0, 24);
    memcpy(mmap_body + 32, e1, 24);
    pos = put_tag(pos, 6, mmap_body, sizeof(mmap_body));

    uint8_t module_body[8 + 7] = {0};
    uint32_t range[2] = {0x200000, 0x300000};

    memcpy(module_body, range, 8);
    memcpy(module_body + 8, "initrd", 7);
    pos = put_tag(pos, 3, module_body, sizeof(module_body));

    uint8_t fb_body[22] = {0};
    uint64_t fb_addr = 0xFD000000u;
    uint32_t fb_dims[3] = {7680, 1920, 1080};

    memcpy(fb_body, &fb_addr, 8);
    memcpy(fb_body + 8, fb_dims, 12);
    fb_body[20] = 32;
    fb_body[21] = 1;
    pos = put_tag(pos, 8, fb_body, sizeof(fb_body));

    uint8_t rsdp[20] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};

    pos = put_tag(pos, 14, rsdp, sizeof(rsdp));
    pos = put_tag(pos, 0, NULL, 0);

    uint32_t total = (uint32_t)(pos - area);

    memcpy(area, &total, 4);
    TEST_ASSERT_EQ_INT(0, multiboot2_init(addr32(area)));

    TEST_ASSERT_EQ_INT(total, multiboot_raw_size());
    TEST_ASSERT_EQ_INT(addr32(area), multiboot_raw_base());
    TEST_ASSERT(multiboot_rsdp() != NULL);
    TEST_ASSERT(memcmp(multiboot_rsdp(), "RSD PTR ", 8) == 0);

    const multiboot_mmap_entry_t *e = multiboot_mmap_first();

    TEST_ASSERT(e != NULL);
    TEST_ASSERT_EQ_INT(0x9FC00, e->len);
    e = multiboot_mmap_next(e);
    TEST_ASSERT(e != NULL);
    TEST_ASSERT_EQ_INT(0x100000, e->addr);
    TEST_ASSERT(multiboot_mmap_next(e) == NULL);

    TEST_ASSERT_EQ_INT(1, multiboot_get_info()->mods_count);
    TEST_ASSERT(multiboot_framebuffer_usable());
    TEST_ASSERT_EQ_INT(1920, multiboot_get_info()->framebuffer_width);
    TEST_ASSERT_EQ_INT(7680, multiboot_get_info()->framebuffer_pitch);
}

/* A Multiboot2 framebuffer tag with colour fields: blue in the low byte is usable, red in the low byte is not. */
static void test_multiboot2_colors(void)
{
    uint8_t fb_body[30] = {0};
    uint64_t fb_addr = 0xFD000000u;
    uint32_t fb_dims[3] = {7680, 1920, 1080};
    static const uint8_t bgrx[6] = {16, 8, 8, 8, 0, 8};
    static const uint8_t rgbx[6] = {0, 8, 8, 8, 16, 8};

    memcpy(fb_body, &fb_addr, 8);
    memcpy(fb_body + 8, fb_dims, 12);
    fb_body[20] = 32;
    fb_body[21] = 1;

    for (int variant = 0; variant < 2; variant++)
    {
        reset();
        memcpy(fb_body + 24, variant == 0 ? bgrx : rgbx, 6);

        uint8_t *pos = put_tag(area + 8, 8, fb_body, sizeof(fb_body));

        pos = put_tag(pos, 0, NULL, 0);

        uint32_t total = (uint32_t)(pos - area);

        memcpy(area, &total, 4);
        TEST_ASSERT_EQ_INT(0, multiboot2_init(addr32(area)));
        TEST_ASSERT_EQ_INT(variant == 0, multiboot_framebuffer_usable());
    }
}

static void test_multiboot2_malformed(void)
{
    reset();

    /* A tag that claims to be larger than the whole list. */
    uint32_t words[6] = {24, 0, 6, 4000, 0, 0};

    memcpy(area, words, sizeof(words));
    TEST_ASSERT_EQ_INT(-1, multiboot2_init(addr32(area)));
    TEST_ASSERT(multiboot_mmap_first() == NULL);

    /* Too short to hold even the end tag. */
    uint32_t tiny[2] = {8, 0};

    memcpy(area, tiny, sizeof(tiny));
    TEST_ASSERT_EQ_INT(-1, multiboot2_init(addr32(area)));

    /* Multiboot 1 afterwards: no raw list, no RSDP. */
    reset();
    boot(0, table);
    TEST_ASSERT_EQ_INT(0, multiboot_raw_size());
    TEST_ASSERT(multiboot_rsdp() == NULL);
}

int main(void)
{
    area = mmap(NULL, AREA_SIZE, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);

    if (area == MAP_FAILED)
    {
        fprintf(stderr, "mmap(MAP_32BIT) failed\n");
        return 1;
    }

    TEST_RUN(test_entry_layout);
    TEST_RUN(test_no_map_flag);
    TEST_RUN(test_empty_map);
    TEST_RUN(test_walk_entries);
    TEST_RUN(test_larger_entry_uses_size_field);
    TEST_RUN(test_truncated_entry_rejected);
    TEST_RUN(test_short_size_rejected);
    TEST_RUN(test_hal_memory_regions);
    TEST_RUN(test_hal_memory_regions_without_map);
    TEST_RUN(test_hal_reserved_regions);
    TEST_RUN(test_hal_reserved_regions_without_map);
    TEST_RUN(test_framebuffer_usable);
    TEST_RUN(test_hal_modules);
    TEST_RUN(test_hal_modules_flag_clear);
    TEST_RUN(test_multiboot2_tags);
    TEST_RUN(test_multiboot2_colors);
    TEST_RUN(test_multiboot2_malformed);

    munmap(area, AREA_SIZE);

    return TEST_REPORT("multiboot");
}
