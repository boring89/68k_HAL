/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "guest.h"

#include "emu/m68k/vm.h"
#include "hal/mmu.h"
#include "kernel/lib/k_memory.h"
#include "kernel/memory/pmm.h"
#include "kernel/panic/panic.h"

static uint32_t guest_ram_size = M68K_GUEST_RAM_DEFAULT_SIZE;
static uintptr_t guest_frames; /* physical address, 0 while not allocated */
static uint32_t guest_pages;   /* size of the allocation, in pages */


/* The emulator's way out when it cannot continue (see emu/m68k/vm.h). */
void m68k_vm_fatal(const char *message)
{
    panic("%s", message);
}


/* Unmap the guest RAM and give the frames back. */
static void unmap_guest_ram(void)
{
    for (uint32_t i = 0; i < guest_pages; i++)
    {
        hal_mmu_unmap(M68K_GUEST_RAM_BASE + i * HAL_MMU_PAGE_SIZE);
    }
    pmm_free_frames(guest_frames, guest_pages);
    guest_frames = 0;
    guest_pages = 0;
}


/* Map guest_ram_size / 4 KiB consecutive frames at the guest RAM address. */
static int map_guest_ram(void)
{
    uint32_t pages = guest_ram_size / HAL_MMU_PAGE_SIZE;
    uintptr_t frames = pmm_alloc_frames(pages);

    if (frames == 0)
    {
        return -1;
    }

    for (uint32_t i = 0; i < pages; i++)
    {
        if (hal_mmu_map(M68K_GUEST_RAM_BASE + i * HAL_MMU_PAGE_SIZE,
                        frames + i * HAL_MMU_PAGE_SIZE,
                        HAL_MMU_WRITABLE) != 0)
        {
            while (i > 0)
            {
                i--;
                hal_mmu_unmap(M68K_GUEST_RAM_BASE + i * HAL_MMU_PAGE_SIZE);
            }
            pmm_free_frames(frames, pages);
            return -1;
        }
    }
    guest_frames = frames;
    guest_pages = pages;
    return 0;
}


int m68k_guest_set_size(uint32_t size)
{
    if (size == 0 || size > M68K_GUEST_RAM_MAX_SIZE ||
        size % HAL_MMU_PAGE_SIZE != 0)
    {
        return -1;
    }

    if (guest_frames != 0 && size != guest_ram_size)
    {
        unmap_guest_ram();
    }
    guest_ram_size = size;
    return 0;
}


uint32_t m68k_guest_ram_size(void)
{
    return guest_ram_size;
}


int m68k_guest_init(void)
{
    if (guest_frames == 0 && map_guest_ram() != 0)
    {
        return -1;
    }

    k_memset((void *)M68K_GUEST_RAM_BASE, 0, guest_ram_size);
    return m68k_vm_init((uint8_t *)M68K_GUEST_RAM_BASE, guest_ram_size,
                        M68K_VM_CPU_68030);
}
