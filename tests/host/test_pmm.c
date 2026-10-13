/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* MAP_32BIT needs _GNU_SOURCE under -std=c11. */
#define _GNU_SOURCE

#include "test.h"

#include <stdint.h>
#include <sys/mman.h>

#include "kernel/memory/multiboot.h"
#include "kernel/memory/pmm.h"

/*
 * Fake boot data lives below 4 GiB (MAP_32BIT) because multiboot_info_t
 * holds 32-bit addresses. Linux places MAP_32BIT mappings at 1-2 GiB, so
 * they do not overlap the fake RAM regions used here (below 64 MiB).
 * PMM never touches the fake physical memory, only its own bitmap.
 */
#define AREA_SIZE 4096

#define MiB 0x100000ULL
#define FRAMES(bytes) ((bytes) / PMM_FRAME_SIZE)

static uint8_t *area;
static multiboot_info_t *mbi;
static uint8_t *table_end;

static void reset(void)
{
    memset(area, 0, AREA_SIZE);
    mbi = (multiboot_info_t *)area;
    table_end = area + 256;
}

static void add_region(uint64_t addr, uint64_t len, uint32_t type)
{
    multiboot_mmap_entry_t entry = {20, addr, len, type};

    memcpy(table_end, &entry, sizeof(entry));
    table_end += sizeof(entry);
}

/* Boot with the regions added so far. */
static void boot_with(uint32_t flags, uintptr_t kernel_start,
                      uintptr_t kernel_end)
{
    mbi->flags = flags;
    mbi->mmap_addr = (uint32_t)(uintptr_t)(area + 256);
    mbi->mmap_length = (uint32_t)(table_end - (area + 256));
    multiboot_init((uint32_t)(uintptr_t)mbi);
    pmm_init(kernel_start, kernel_end);
}

/* Boot with a memory map and an empty kernel range. */
static void boot(void)
{
    boot_with(MULTIBOOT_INFO_MEM_MAP, MiB, MiB);
}

static void test_qemu_like_map(void)
{
    reset();
    add_region(0x0, 0x9FC00, MULTIBOOT_MEMORY_AVAILABLE);
    add_region(0x9FC00, 0x400, MULTIBOOT_MEMORY_RESERVED);
    add_region(0xF0000, 0x10000, MULTIBOOT_MEMORY_RESERVED);
    add_region(MiB, 15 * MiB, MULTIBOOT_MEMORY_AVAILABLE);

    /* Kernel at 0x100000 - 0x208E00 touches frames 0x100 - 0x208. */
    boot_with(MULTIBOOT_INFO_MEM_MAP, 0x100000, 0x208E00);

    /* 0x9FC00 / 4 KiB = 159 whole frames (partial last frame dropped). */
    TEST_ASSERT_EQ_INT(159 + FRAMES(15 * MiB), pmm_get_total_frames());

    /* Low 1 MiB and 0x109 kernel frames are reserved. */
    TEST_ASSERT_EQ_INT(FRAMES(15 * MiB) - 0x109, pmm_get_free_frames());
}

static void test_no_memory_map(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot_with(0, MiB, MiB);

    TEST_ASSERT_EQ_INT(0, pmm_get_total_frames());
    TEST_ASSERT_EQ_INT(0, pmm_get_free_frames());
}

static void test_unaligned_region_rounds_inward(void)
{
    reset();
    add_region(0x100800, 0x3000, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    /* 0x100800 - 0x103800: only frames 0x101 and 0x102 are whole. */
    TEST_ASSERT_EQ_INT(2, pmm_get_total_frames());
    TEST_ASSERT_EQ_INT(2, pmm_get_free_frames());
}

static void test_low_memory_reserved(void)
{
    reset();
    add_region(0x0, 0x9FC00, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    TEST_ASSERT_EQ_INT(159, pmm_get_total_frames());
    TEST_ASSERT_EQ_INT(0, pmm_get_free_frames());
}

static void test_reserved_overlap_wins(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    add_region(0x180800, 0x100, MULTIBOOT_MEMORY_RESERVED);
    boot();

    TEST_ASSERT_EQ_INT(256, pmm_get_total_frames());
    TEST_ASSERT_EQ_INT(255, pmm_get_free_frames());
}

static void test_above_4gib_clamped(void)
{
    reset();
    add_region(0x100000000ULL, 16 * MiB, MULTIBOOT_MEMORY_AVAILABLE);
    add_region(0xFFFF0000ULL, 0x20000, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    /* Only 0xFFFF0000 - 0xFFFFFFFF (16 frames) is addressable. */
    TEST_ASSERT_EQ_INT(16, pmm_get_total_frames());
    TEST_ASSERT_EQ_INT(16, pmm_get_free_frames());
}

static void test_reserve_range(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    /* 0x150800 - 0x151800 touches frames 0x150 and 0x151. */
    TEST_ASSERT_EQ_INT(0, pmm_reserve_range(0x150800, 0x1000));
    TEST_ASSERT_EQ_INT(254, pmm_get_free_frames());

    /* Reserving again changes nothing. */
    TEST_ASSERT_EQ_INT(0, pmm_reserve_range(0x150000, 0x2000));
    TEST_ASSERT_EQ_INT(254, pmm_get_free_frames());

    TEST_ASSERT_EQ_INT(-1, pmm_reserve_range(0x160000, 0));
    TEST_ASSERT_EQ_INT(254, pmm_get_free_frames());
}

static void test_init_resets_state(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();
    pmm_reserve_range(MiB, MiB);
    TEST_ASSERT_EQ_INT(0, pmm_get_free_frames());

    pmm_init(MiB, MiB);
    TEST_ASSERT_EQ_INT(256, pmm_get_free_frames());
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

    TEST_RUN(test_qemu_like_map);
    TEST_RUN(test_no_memory_map);
    TEST_RUN(test_unaligned_region_rounds_inward);
    TEST_RUN(test_low_memory_reserved);
    TEST_RUN(test_reserved_overlap_wins);
    TEST_RUN(test_above_4gib_clamped);
    TEST_RUN(test_reserve_range);
    TEST_RUN(test_init_resets_state);

    munmap(area, AREA_SIZE);

    return TEST_REPORT("pmm");
}
