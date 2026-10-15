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

#include <stdbool.h>

#define GUEST_PAGES (M68K_GUEST_RAM_SIZE / HAL_MMU_PAGE_SIZE)

static bool guest_ram_mapped;


/* The emulator's way out when it cannot continue (see emu/m68k/vm.h). */
void m68k_vm_fatal(const char *message)
{
    panic("%s", message);
}


/* Map GUEST_PAGES consecutive frames at the guest RAM address. */
static int map_guest_ram(void)
{
    uintptr_t frames = pmm_alloc_frames(GUEST_PAGES);

    if (frames == 0)
    {
        return -1;
    }

    for (uint32_t i = 0; i < GUEST_PAGES; i++)
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
            pmm_free_frames(frames, GUEST_PAGES);
            return -1;
        }
    }
    return 0;
}


int m68k_guest_init(void)
{
    if (!guest_ram_mapped)
    {
        if (map_guest_ram() != 0)
        {
            return -1;
        }
        guest_ram_mapped = true;
    }

    k_memset((void *)M68K_GUEST_RAM_BASE, 0, M68K_GUEST_RAM_SIZE);
    return m68k_vm_init((uint8_t *)M68K_GUEST_RAM_BASE, M68K_GUEST_RAM_SIZE,
                        M68K_VM_CPU_68030);
}
