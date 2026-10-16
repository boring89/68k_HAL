/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/framebuffer.h"

#include "framebuffer.h"

#include "hal/mmu.h"

#include "platform/pc/boot/multiboot.h"

#include <stddef.h>

static hal_framebuffer_t framebuffer;
static bool framebuffer_mapped;

bool pc_framebuffer_detect(void)
{
    if (!multiboot_framebuffer_usable())
        return false;

    const multiboot_info_t *mbi = multiboot_get_info();

    framebuffer.base = (uint8_t *)(uintptr_t)mbi->framebuffer_addr;
    framebuffer.width = mbi->framebuffer_width;
    framebuffer.height = mbi->framebuffer_height;
    framebuffer.pitch = mbi->framebuffer_pitch;
    framebuffer.bpp = mbi->framebuffer_bpp;

    return true;
}

int pc_framebuffer_map(void)
{
    if (!pc_framebuffer_detect())
        return -1;

    uint64_t start = (uintptr_t)framebuffer.base;
    uint64_t end = start + (uint64_t)framebuffer.pitch * framebuffer.height;

    for (uint64_t page = start & ~(uint64_t)(HAL_MMU_PAGE_SIZE - 1);
         page < end; page += HAL_MMU_PAGE_SIZE)
    {
        uintptr_t phys;

        /* The boot data mapping may already cover the first page. */
        if (hal_mmu_get_phys((uintptr_t)page, &phys) == 0)
            continue;

        if (hal_mmu_map((uintptr_t)page, (uintptr_t)page,
                        HAL_MMU_WRITABLE) != 0)
            return -1;
    }

    framebuffer_mapped = true;

    return 0;
}

int hal_framebuffer_get(hal_framebuffer_t *fb)
{
    if (fb == NULL || !framebuffer_mapped)
        return -1;

    *fb = framebuffer;

    return 0;
}
