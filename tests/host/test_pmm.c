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

static void test_alloc_lowest_first(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    TEST_ASSERT_EQ_INT(0x100000, pmm_alloc_frame());
    TEST_ASSERT_EQ_INT(0x101000, pmm_alloc_frame());
    TEST_ASSERT_EQ_INT(254, pmm_get_free_frames());
}

static void test_alloc_skips_kernel_and_reserved(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot_with(MULTIBOOT_INFO_MEM_MAP, 0x100000, 0x108800);
    pmm_reserve_range(0x10A000, 0x1000);

    TEST_ASSERT_EQ_INT(0x109000, pmm_alloc_frame());
    TEST_ASSERT_EQ_INT(0x10B000, pmm_alloc_frame());
}

static void test_alloc_across_word_boundary(void)
{
    reset();

    /* Frames 0x11F (word 8, bit 31) and 0x120 (word 9, bit 0). */
    add_region(0x11F000, 0x2000, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    TEST_ASSERT_EQ_INT(0x11F000, pmm_alloc_frame());
    TEST_ASSERT_EQ_INT(0x120000, pmm_alloc_frame());
    TEST_ASSERT_EQ_INT(0, pmm_alloc_frame());
}

static void test_alloc_across_hole(void)
{
    reset();
    add_region(MiB, 0x1000, MULTIBOOT_MEMORY_AVAILABLE);
    add_region(2 * MiB, 0x1000, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    TEST_ASSERT_EQ_INT(0x100000, pmm_alloc_frame());
    TEST_ASSERT_EQ_INT(0x200000, pmm_alloc_frame());
    TEST_ASSERT_EQ_INT(0, pmm_alloc_frame());
}

static void test_alloc_until_exhausted(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    for (int i = 0; i < 256; i++)
    {
        uintptr_t frame = pmm_alloc_frame();

        TEST_ASSERT_EQ_INT(MiB + (uintptr_t)i * PMM_FRAME_SIZE, frame);
    }

    TEST_ASSERT_EQ_INT(0, pmm_get_free_frames());
    TEST_ASSERT_EQ_INT(0, pmm_alloc_frame());
    TEST_ASSERT_EQ_INT(0, pmm_alloc_frame());
}

static void test_alloc_without_memory(void)
{
    reset();
    boot_with(0, MiB, MiB);

    TEST_ASSERT_EQ_INT(0, pmm_alloc_frame());
}

static void test_free_then_reuse(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    uintptr_t a = pmm_alloc_frame();
    uintptr_t b = pmm_alloc_frame();
    uintptr_t c = pmm_alloc_frame();

    TEST_ASSERT_EQ_INT(253, pmm_get_free_frames());

    /* Freeing below the hint pulls it back: b is handed out again. */
    TEST_ASSERT_EQ_INT(0, pmm_free_frame(b));
    TEST_ASSERT_EQ_INT(254, pmm_get_free_frames());
    TEST_ASSERT_EQ_INT(b, pmm_alloc_frame());

    /* Lowest free frame wins when several are freed. */
    TEST_ASSERT_EQ_INT(0, pmm_free_frame(c));
    TEST_ASSERT_EQ_INT(0, pmm_free_frame(a));
    TEST_ASSERT_EQ_INT(a, pmm_alloc_frame());
    TEST_ASSERT_EQ_INT(c, pmm_alloc_frame());
}

static void test_free_after_exhaustion(void)
{
    reset();
    add_region(MiB, 0x3000, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    pmm_alloc_frame();
    uintptr_t mid = pmm_alloc_frame();
    pmm_alloc_frame();
    TEST_ASSERT_EQ_INT(0, pmm_alloc_frame());

    TEST_ASSERT_EQ_INT(0, pmm_free_frame(mid));
    TEST_ASSERT_EQ_INT(mid, pmm_alloc_frame());
}

static void test_free_rejects_double_free(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    uintptr_t a = pmm_alloc_frame();

    TEST_ASSERT_EQ_INT(0, pmm_free_frame(a));
    TEST_ASSERT_EQ_INT(-1, pmm_free_frame(a));
    TEST_ASSERT_EQ_INT(256, pmm_get_free_frames());

    /* Never allocated, still free. */
    TEST_ASSERT_EQ_INT(-1, pmm_free_frame(0x180000));
    TEST_ASSERT_EQ_INT(256, pmm_get_free_frames());
}

static void test_free_rejects_bad_addresses(void)
{
    reset();
    add_region(0x0, 0x9FC00, MULTIBOOT_MEMORY_AVAILABLE);
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    uintptr_t a = pmm_alloc_frame();
    size_t free_before = pmm_get_free_frames();

    TEST_ASSERT_EQ_INT(-1, pmm_free_frame(0));
    TEST_ASSERT_EQ_INT(-1, pmm_free_frame(0xB8000));      /* VGA */
    TEST_ASSERT_EQ_INT(-1, pmm_free_frame(0x1000));       /* low RAM */
    TEST_ASSERT_EQ_INT(-1, pmm_free_frame(a + 0x10));     /* unaligned */
    TEST_ASSERT_EQ_INT(-1, pmm_free_frame(2 * MiB));      /* at limit */
    TEST_ASSERT_EQ_INT(-1, pmm_free_frame(0xFFFFF000));   /* past RAM */

    TEST_ASSERT_EQ_INT(free_before, pmm_get_free_frames());
    TEST_ASSERT_EQ_INT(0, pmm_free_frame(a));
}

static void test_alloc_frames_rejects_bad_count(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    TEST_ASSERT_EQ_INT(0, pmm_alloc_frames(0));
    TEST_ASSERT_EQ_INT(0, pmm_alloc_frames(257));
    TEST_ASSERT_EQ_INT(256, pmm_get_free_frames());
}

static void test_alloc_frames_basic(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    /* count = 1 behaves like pmm_alloc_frame. */
    TEST_ASSERT_EQ_INT(0x100000, pmm_alloc_frames(1));

    TEST_ASSERT_EQ_INT(0x101000, pmm_alloc_frames(3));
    TEST_ASSERT_EQ_INT(252, pmm_get_free_frames());

    /* Run started at the first free frame: hint moves past it. */
    TEST_ASSERT_EQ_INT(0x104000, pmm_alloc_frame());
}

static void test_alloc_frames_skips_short_gap(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    for (int i = 0; i < 4; i++)
        pmm_alloc_frame();

    /* Gap of 2 at 0x100-0x101, then 0x102-0x103 used. */
    pmm_free_frame(0x100000);
    pmm_free_frame(0x101000);

    TEST_ASSERT_EQ_INT(0x104000, pmm_alloc_frames(3));

    /* The skipped gap is still found by the next allocation. */
    TEST_ASSERT_EQ_INT(0x100000, pmm_alloc_frame());
}

static void test_alloc_frames_skips_full_words(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    /* Use frames 0x100-0x15F (words 8-10), then free 0x100 only. */
    for (int i = 0; i < 96; i++)
        pmm_alloc_frame();

    pmm_free_frame(0x100000);

    TEST_ASSERT_EQ_INT(0x160000, pmm_alloc_frames(2));
    TEST_ASSERT_EQ_INT(0x100000, pmm_alloc_frame());
}

static void test_alloc_frames_across_word_boundary(void)
{
    reset();

    /* Frames 0x11E-0x121 span words 8 and 9. */
    add_region(0x11E000, 0x4000, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    TEST_ASSERT_EQ_INT(0x11E000, pmm_alloc_frames(4));
    TEST_ASSERT_EQ_INT(0, pmm_alloc_frame());
}

static void test_alloc_frames_not_across_hole(void)
{
    reset();
    add_region(MiB, 0x2000, MULTIBOOT_MEMORY_AVAILABLE);
    add_region(MiB + 0x3000, 0x2000, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    /* Frame 0x102 is not RAM, so 4 frames never fit. */
    TEST_ASSERT_EQ_INT(4, pmm_get_free_frames());
    TEST_ASSERT_EQ_INT(0, pmm_alloc_frames(3));
    TEST_ASSERT_EQ_INT(0x100000, pmm_alloc_frames(2));
    TEST_ASSERT_EQ_INT(0x103000, pmm_alloc_frames(2));
}

static void test_alloc_frames_fragmented(void)
{
    reset();
    add_region(MiB, 0x4000, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    for (int i = 0; i < 4; i++)
        pmm_alloc_frame();

    pmm_free_frame(0x100000);
    pmm_free_frame(0x102000);

    /* 2 frames free, but not next to each other. */
    TEST_ASSERT_EQ_INT(0, pmm_alloc_frames(2));
    TEST_ASSERT_EQ_INT(2, pmm_get_free_frames());

    /* Failure leaves the hint alone. */
    TEST_ASSERT_EQ_INT(0x100000, pmm_alloc_frame());
}

static void test_free_frames_then_reuse(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    uintptr_t a = pmm_alloc_frames(4);
    uintptr_t b = pmm_alloc_frames(4);

    TEST_ASSERT_EQ_INT(0, pmm_free_frames(a, 4));
    TEST_ASSERT_EQ_INT(252, pmm_get_free_frames());

    /* Hint pulled back to a: the block is reused. */
    TEST_ASSERT_EQ_INT(a, pmm_alloc_frames(4));
    TEST_ASSERT_EQ_INT(0, pmm_free_frames(b, 4));
    TEST_ASSERT_EQ_INT(b, pmm_alloc_frame());
}

static void test_free_frames_all_or_nothing(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    uintptr_t a = pmm_alloc_frames(3);

    /* Middle frame already free: nothing is freed. */
    TEST_ASSERT_EQ_INT(0, pmm_free_frame(a + PMM_FRAME_SIZE));
    TEST_ASSERT_EQ_INT(254, pmm_get_free_frames());

    TEST_ASSERT_EQ_INT(-1, pmm_free_frames(a, 3));
    TEST_ASSERT_EQ_INT(254, pmm_get_free_frames());

    /* The first frame is still used, so it can be freed alone. */
    TEST_ASSERT_EQ_INT(0, pmm_free_frame(a));
}

static void test_free_frames_rejects_bad_ranges(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    uintptr_t a = pmm_alloc_frames(256);
    TEST_ASSERT_EQ_INT(MiB, a);

    TEST_ASSERT_EQ_INT(-1, pmm_free_frames(a, 0));
    TEST_ASSERT_EQ_INT(-1, pmm_free_frames(a + 0x10, 1));
    TEST_ASSERT_EQ_INT(-1, pmm_free_frames(0xFF000, 2));     /* starts low */

    /* Runs past the limit by one frame. */
    TEST_ASSERT_EQ_INT(-1, pmm_free_frames(a, 257));

    /* count * 4096 would overflow; must not wrap around. */
    TEST_ASSERT_EQ_INT(-1, pmm_free_frames(a, SIZE_MAX));
    TEST_ASSERT_EQ_INT(-1, pmm_free_frames(a, SIZE_MAX / PMM_FRAME_SIZE + 1));

    TEST_ASSERT_EQ_INT(0, pmm_get_free_frames());
    TEST_ASSERT_EQ_INT(0, pmm_free_frames(a, 256));
    TEST_ASSERT_EQ_INT(256, pmm_get_free_frames());
}

static void test_free_frames_spanning_allocations(void)
{
    reset();
    add_region(MiB, MiB, MULTIBOOT_MEMORY_AVAILABLE);
    boot();

    /* Accepted limitation: block boundaries are not recorded. */
    uintptr_t a = pmm_alloc_frames(2);
    pmm_alloc_frames(2);

    TEST_ASSERT_EQ_INT(0, pmm_free_frames(a, 4));
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
    TEST_RUN(test_alloc_lowest_first);
    TEST_RUN(test_alloc_skips_kernel_and_reserved);
    TEST_RUN(test_alloc_across_word_boundary);
    TEST_RUN(test_alloc_across_hole);
    TEST_RUN(test_alloc_until_exhausted);
    TEST_RUN(test_alloc_without_memory);
    TEST_RUN(test_free_then_reuse);
    TEST_RUN(test_free_after_exhaustion);
    TEST_RUN(test_free_rejects_double_free);
    TEST_RUN(test_free_rejects_bad_addresses);
    TEST_RUN(test_alloc_frames_rejects_bad_count);
    TEST_RUN(test_alloc_frames_basic);
    TEST_RUN(test_alloc_frames_skips_short_gap);
    TEST_RUN(test_alloc_frames_skips_full_words);
    TEST_RUN(test_alloc_frames_across_word_boundary);
    TEST_RUN(test_alloc_frames_not_across_hole);
    TEST_RUN(test_alloc_frames_fragmented);
    TEST_RUN(test_free_frames_then_reuse);
    TEST_RUN(test_free_frames_all_or_nothing);
    TEST_RUN(test_free_frames_rejects_bad_ranges);
    TEST_RUN(test_free_frames_spanning_allocations);

    munmap(area, AREA_SIZE);

    return TEST_REPORT("pmm");
}
