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

    pmm_mark_free(first, last);
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

size_t pmm_get_total_frames(void)
{
    return pmm_total_count;
}

size_t pmm_get_free_frames(void)
{
    return pmm_free_count;
}
