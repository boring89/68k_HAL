/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/mmu.h"

#include "space.h"
#include "paging.h"

#include "arch/x86/cpu/cpu.h"
#include "kernel/lib/k_memory.h"
#include "kernel/memory/pmm.h"

/*
 * The kernel keeps every directory permanently mapped in a window, so it can edit the
 * directory of a space that is not the current one (the current one is also reachable through
 * the recursive mapping, but the window works for all). Two scratch pages next to it show a
 * single frame for a moment (a page table of another space, or a page that is being copied).
 *
 *   0xD0000000 + n * 4 KiB   directory of space n (n = 0 is the kernel's)
 *   0xD0100000, 0xD0101000   scratch pages
 *
 * Page tables of the window itself are shared (the entry for 0xD0000000 is created by the
 * first mapping, before any other space exists).
 */
#define WINDOW_BASE 0xD0000000u
#define SCRATCH_A (WINDOW_BASE + 0x100000u)
#define SCRATCH_B (WINDOW_BASE + 0x101000u)

typedef struct
{
    uintptr_t phys; /* the directory's frame, 0 = slot free */
    paging_entry_t *dir;
} space_slot_t;

static space_slot_t slot[HAL_SPACE_MAX];
static bool ready;

/*
 * The user ranges of kernel/user/process.h (USER_IMAGE_MIN..USER_BRK_LIMIT rounded to 4 MiB,
 * and USER_MMAP_BASE..USER_STACK_TOP). Everything else is the kernel's: the identity map of the
 * low 4 MiB, the guest RAM, the heap, device windows.
 */
#define PRIVATE_LOW_START 0x08000000u
#define PRIVATE_LOW_END 0x10000000u
#define PRIVATE_HIGH_START 0x50000000u
#define PRIVATE_HIGH_END 0x80000000u

bool space_index_is_private(uint32_t dir_index)
{
    uint32_t address = dir_index << 22;

    return (address >= PRIVATE_LOW_START && address < PRIVATE_LOW_END) ||
           (address >= PRIVATE_HIGH_START && address < PRIVATE_HIGH_END);
}

static int find_slot(hal_space_t space)
{
    for (int i = 0; i < HAL_SPACE_MAX; i++)
    {
        if (slot[i].phys == space && space != 0)
            return i;
    }

    return -1;
}

static void space_init(void)
{
    uintptr_t kernel_dir = cpu_read_cr3() & ~(uintptr_t)(PMM_FRAME_SIZE - 1);

    if (paging_map(WINDOW_BASE, kernel_dir, PAGING_WRITABLE) != 0)
        return;

    slot[0].phys = kernel_dir;
    slot[0].dir = (paging_entry_t *)WINDOW_BASE;
    ready = true;
}

void space_propagate_entry(uint32_t dir_index, paging_entry_t entry)
{
    if (!ready)
        return;

    for (int i = 0; i < HAL_SPACE_MAX; i++)
    {
        if (slot[i].phys != 0)
            slot[i].dir[dir_index] = entry;
    }
}

hal_space_t hal_space_current(void)
{
    return cpu_read_cr3() & ~(uintptr_t)(PMM_FRAME_SIZE - 1);
}

hal_space_t hal_space_create(void)
{
    if (!ready)
        space_init();

    if (!ready)
        return 0;

    int index = -1;

    for (int i = 1; i < HAL_SPACE_MAX && index < 0; i++)
    {
        if (slot[i].phys == 0)
            index = i;
    }

    if (index < 0)
        return 0;

    uintptr_t frame = pmm_alloc_frame();

    if (frame == 0)
        return 0;

    uintptr_t window = WINDOW_BASE + (uintptr_t)index * PMM_FRAME_SIZE;

    if (paging_map(window, frame, PAGING_WRITABLE) != 0)
    {
        pmm_free_frame(frame);

        return 0;
    }

    paging_entry_t *dir = (paging_entry_t *)window;

    k_memset(dir, 0, PMM_FRAME_SIZE);

    for (uint32_t i = 0; i < PAGING_RECURSIVE_INDEX; i++)
    {
        if (!space_index_is_private(i))
            dir[i] = slot[0].dir[i];
    }

    dir[PAGING_RECURSIVE_INDEX] = paging_entry_make(frame, PAGING_PRESENT | PAGING_WRITABLE);

    slot[index].phys = frame;
    slot[index].dir = dir;

    return frame;
}

void hal_space_switch(hal_space_t space)
{
    cpu_write_cr3((uint32_t)space);
}

int hal_space_destroy(hal_space_t space)
{
    int index = find_slot(space);

    if (index <= 0 || space == hal_space_current())
        return -1;

    for (uint32_t i = 0; i < PAGING_RECURSIVE_INDEX; i++)
    {
        if (space_index_is_private(i) && paging_entry_is_present(slot[index].dir[i]))
            pmm_free_frame(paging_entry_get_phys(slot[index].dir[i]));
    }

    paging_unmap((uintptr_t)slot[index].dir);
    pmm_free_frame(slot[index].phys);
    slot[index].phys = 0;
    slot[index].dir = 0;

    return 0;
}

/* Map frame at the scratch page and return it. */
static void *scratch_show(uintptr_t scratch, uintptr_t frame)
{
    if (paging_map(scratch, frame, PAGING_WRITABLE) != 0)
        return 0;

    return (void *)scratch;
}

static int map_in(hal_space_t space, uintptr_t virt, uintptr_t phys, uint32_t pte_flags)
{
    if (virt % PMM_FRAME_SIZE != 0 || phys % PMM_FRAME_SIZE != 0)
        return -1;

    uint32_t dir_index = paging_dir_index(virt);

    if (!space_index_is_private(dir_index))
        return -1;

    int index = find_slot(space);

    if (index < 0)
        return -1;

    paging_entry_t *dir = slot[index].dir;

    if (!paging_entry_is_present(dir[dir_index]))
    {
        uintptr_t table = pmm_alloc_frame();

        if (table == 0)
            return -1;

        paging_entry_t *view = scratch_show(SCRATCH_A, table);

        if (view == 0)
        {
            pmm_free_frame(table);

            return -1;
        }

        k_memset(view, 0, PMM_FRAME_SIZE);
        paging_unmap(SCRATCH_A);

        /* Same permissive directory flags as paging_get_or_create_table. */
        dir[dir_index] = paging_entry_make(table, PAGING_PRESENT | PAGING_WRITABLE | PAGING_USER);
    }

    paging_entry_t *table = scratch_show(SCRATCH_A, paging_entry_get_phys(dir[dir_index]));

    if (table == 0)
        return -1;

    int status = -1;
    paging_entry_t *entry = &table[paging_table_index(virt)];

    if (!paging_entry_is_present(*entry))
    {
        *entry = paging_entry_make(phys, pte_flags | PAGING_PRESENT);
        status = 0;
    }

    paging_unmap(SCRATCH_A);

    return status;
}

int hal_space_map(hal_space_t space, uintptr_t virt, uintptr_t phys, uint32_t flags)
{
    uint32_t paging_flags;

    if (paging_flags_from_hal(flags, &paging_flags) != 0)
        return -1;

    if (space == hal_space_current())
        return paging_map(virt, phys, paging_flags);

    return map_in(space, virt, phys, paging_flags);
}

int hal_space_copy_page(hal_space_t dst, uintptr_t virt)
{
    uint32_t flags;

    if (paging_get_flags(virt, &flags) != 0)
        return -1;

    uintptr_t frame = pmm_alloc_frame();

    if (frame == 0)
        return -1;

    void *view = scratch_show(SCRATCH_B, frame);

    if (view == 0)
    {
        pmm_free_frame(frame);

        return -1;
    }

    k_memcpy(view, (const void *)virt, PMM_FRAME_SIZE);
    paging_unmap(SCRATCH_B);

    if (map_in(dst, virt, frame, flags & 0xFFF) != 0)
    {
        pmm_free_frame(frame);

        return -1;
    }

    return 0;
}
