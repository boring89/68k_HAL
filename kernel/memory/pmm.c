/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pmm.h"

#include "multiboot.h"
#include "../lib/k_memory.h"

/* 32-bit physical address space without PAE: 4 GiB / 4 KiB frames. */
#define PMM_MAX_FRAMES (1u << 20)
#define PMM_ADDRESS_LIMIT ((uint64_t)PMM_MAX_FRAMES * PMM_FRAME_SIZE)

/* The low 1 MiB (BIOS data, EBDA, VGA, ROMs, frame 0) is never handed out. */
#define PMM_LOW_MEMORY_END 0x100000

/* One bit per frame, 1 = used. 128 KiB in .bss covers all of 4 GiB. */
static uint32_t pmm_bitmap[PMM_MAX_FRAMES / 32];

static size_t pmm_total_count;
static size_t pmm_free_count;

/* Search start: every frame below pmm_hint is used. */
static uint32_t pmm_hint;

/* One past the highest available frame; nothing at or above is ever free. */
static uint32_t pmm_limit;

static int pmm_test(uint32_t frame)
{
    return (pmm_bitmap[frame / 32] >> (frame % 32)) & 1;
}

static void pmm_set(uint32_t frame)
{
    pmm_bitmap[frame / 32] |= 1u << (frame % 32);
}

static void pmm_clear(uint32_t frame)
{
    pmm_bitmap[frame / 32] &= ~(1u << (frame % 32));
}

/* Clamp a 64-bit address to what the bitmap covers. */
static uint64_t pmm_clamp(uint64_t address)
{
    return address > PMM_ADDRESS_LIMIT ? PMM_ADDRESS_LIMIT : address;
}

/* Mark frames [first, end) used; frames already used are skipped. */
static void pmm_mark_used(
    uint64_t first,
    uint64_t end)
{
    for (uint64_t frame = first; frame < end; frame++)
    {
        if (!pmm_test((uint32_t)frame))
        {
            pmm_set((uint32_t)frame);
            pmm_free_count--;
        }
    }
}

/* Mark frames [first, end) free; frames already free are skipped. */
static void pmm_mark_free(
    uint64_t first,
    uint64_t end)
{
    for (uint64_t frame = first; frame < end; frame++)
    {
        if (pmm_test((uint32_t)frame))
        {
            pmm_clear((uint32_t)frame);
            pmm_free_count++;
        }
    }
}

/* Available region: only whole frames count, so round inward. */
static void pmm_free_region(
    uint64_t start,
    uint64_t end)
{
    uint64_t first =
        (pmm_clamp(start) + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE;
    uint64_t last =
        pmm_clamp(end) / PMM_FRAME_SIZE;

    if (first >= last)
        return;

    pmm_mark_free(first, last);

    if (last > pmm_limit)
        pmm_limit = (uint32_t)last;
}

/* Used region: any frame it touches is used, so round outward. */
static void pmm_reserve_region(
    uint64_t start,
    uint64_t end)
{
    uint64_t first =
        pmm_clamp(start) / PMM_FRAME_SIZE;
    uint64_t last =
        (pmm_clamp(end) + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE;

    pmm_mark_used(first, last);
}

void pmm_init(
    uintptr_t kernel_start,
    uintptr_t kernel_end)
{
    k_memset(pmm_bitmap, 0xFF, sizeof(pmm_bitmap));
    pmm_free_count = 0;
    pmm_hint = 0;
    pmm_limit = 0;

    const multiboot_mmap_entry_t *entry;

    /* Pass 1: free every available region. */
    for (entry = multiboot_mmap_first();
         entry != NULL;
         entry = multiboot_mmap_next(entry))
    {
        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE)
            pmm_free_region(entry->addr, entry->addr + entry->len);
    }

    pmm_total_count = pmm_free_count;

    /* Pass 2: overlapping non-available regions win (stay conservative). */
    for (entry = multiboot_mmap_first();
         entry != NULL;
         entry = multiboot_mmap_next(entry))
    {
        if (entry->type != MULTIBOOT_MEMORY_AVAILABLE)
            pmm_reserve_region(entry->addr, entry->addr + entry->len);
    }

    /* Pass 3: memory that is RAM but already in use. */
    pmm_reserve_region(0, PMM_LOW_MEMORY_END);
    pmm_reserve_region(kernel_start, kernel_end);

    const multiboot_info_t *mbi = multiboot_get_info();
    uint64_t mbi_address = (uintptr_t)mbi;

    pmm_reserve_region(mbi_address, mbi_address + sizeof(*mbi));

    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP)
    {
        pmm_reserve_region(
            mbi->mmap_addr,
            (uint64_t)mbi->mmap_addr + mbi->mmap_length);
    }
}

int pmm_reserve_range(
    uintptr_t start,
    size_t size)
{
    if (size == 0)
        return -1;

    pmm_reserve_region(start, (uint64_t)start + size);

    return 0;
}

uintptr_t pmm_alloc_frame(void)
{
    for (uint32_t word = pmm_hint / 32; word * 32 < pmm_limit; word++)
    {
        /* 32 used frames: skip the whole word. */
        if (pmm_bitmap[word] == 0xFFFFFFFF)
            continue;

        /* Lowest 0 bit = number of trailing zeros of the inverted word
         * (BSF, Intel SDM Vol. 2A). ~word != 0 here, so ctz is defined. */
        uint32_t frame =
            word * 32 + (uint32_t)__builtin_ctz(~pmm_bitmap[word]);

        if (frame >= pmm_limit)
            break;

        pmm_set(frame);
        pmm_free_count--;
        pmm_hint = frame + 1;

        return (uintptr_t)frame * PMM_FRAME_SIZE;
    }

    /* Nothing free below the limit. */
    pmm_hint = pmm_limit;

    return 0;
}

uintptr_t pmm_alloc_frames(size_t count)
{
    /* Zero is meaningless; more than the free count can never fit. */
    if (count == 0 || count > pmm_free_count)
        return 0;

    uint32_t first_free = pmm_limit;
    uint32_t run_start = 0;
    size_t run_length = 0;

    /* One pass: count consecutive free frames, reset on a used one. */
    for (uint32_t frame = pmm_hint; frame < pmm_limit; frame++)
    {
        if (frame % 32 == 0 && pmm_bitmap[frame / 32] == 0xFFFFFFFF)
        {
            /* 32 used frames: break the run and skip the word. */
            run_length = 0;
            frame += 31;
            continue;
        }

        if (pmm_test(frame))
        {
            run_length = 0;
            continue;
        }

        if (first_free == pmm_limit)
            first_free = frame;

        if (run_length == 0)
            run_start = frame;

        run_length++;

        if (run_length == count)
        {
            pmm_mark_used(run_start, (uint64_t)run_start + count);

            /* Free frames skipped before the run keep the hint there. */
            pmm_hint = run_start == first_free
                           ? run_start + (uint32_t)count
                           : first_free;

            return (uintptr_t)run_start * PMM_FRAME_SIZE;
        }
    }

    /* No run long enough; scattered free frames may remain, so the hint
     * stays where it is. */
    return 0;
}

int pmm_free_frame(uintptr_t address)
{
    return pmm_free_frames(address, 1);
}

int pmm_free_frames(
    uintptr_t address,
    size_t count)
{
    /* Check range and alignment before touching the bitmap. The low
     * 1 MiB is never allocated, so freeing it (or 0) is always a bug. */
    if (count == 0 ||
        address % PMM_FRAME_SIZE != 0 ||
        address < PMM_LOW_MEMORY_END ||
        address / PMM_FRAME_SIZE >= pmm_limit)
        return -1;

    uint32_t first = (uint32_t)(address / PMM_FRAME_SIZE);

    /* Compare counts instead of computing an end address: no overflow. */
    if (count > pmm_limit - first)
        return -1;

    uint32_t end = first + (uint32_t)count;

    /* All or nothing: any frame already free is a double free. */
    for (uint32_t frame = first; frame < end; frame++)
    {
        if (!pmm_test(frame))
            return -1;
    }

    for (uint32_t frame = first; frame < end; frame++)
        pmm_clear(frame);

    pmm_free_count += count;

    /* Keep "every frame below pmm_hint is used" true. */
    if (first < pmm_hint)
        pmm_hint = first;

    return 0;
}

size_t pmm_get_total_frames(void)
{
    return pmm_total_count;
}

size_t pmm_get_free_frames(void)
{
    return pmm_free_count;
}
