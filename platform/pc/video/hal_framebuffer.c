/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/framebuffer.h"

#include "framebuffer.h"

#include "hal/mmu.h"

#include "drivers/bga/bga.h"

#include "platform/pc/vmap/vmap.h"

#include "platform/pc/boot/multiboot.h"
#include "platform/pc/video/vbe.h"

#include "hal/boot.h"
#include "kernel/lib/k_string.h"

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

#ifdef HAL_PHYS_BASE
    /*
     * A 64-bit kernel maps the screen in its own window, which every address space shares (a mapping
     * of the lower half would exist only in the address space that made it), and may find it above 4 GiB.
     */
    void *window = vmap(start, (size_t)(end - start), HAL_MMU_WRITABLE | HAL_MMU_WRITE_COMBINING);

    if (window == NULL)
        return -1;

    framebuffer.base = window;
#else
    for (uint64_t page = start & ~(uint64_t)(HAL_MMU_PAGE_SIZE - 1);
         page < end; page += HAL_MMU_PAGE_SIZE)
    {
        uintptr_t phys;

        /* The boot data mapping may already cover the first page. */
        if (hal_mmu_get_phys((uintptr_t)page, &phys) == 0)
            continue;

        if (hal_mmu_map((uintptr_t)page, (uintptr_t)page,
                        HAL_MMU_WRITABLE | HAL_MMU_WRITE_COMBINING) != 0)
            return -1;
    }
#endif

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

/* "nobga" on the command line: leave the Bochs/QEMU display interface alone (to test the VBE path). */
static bool bga_allowed(void)
{
    const char *command_line = hal_boot_cmdline();

    return command_line == NULL || k_strstr(command_line, "nobga") == NULL;
}

#define VBE_MODES_MAX 64

/* A VBE mode of exactly width x height: switch to it. Returns 0 and the framebuffer, or -1. */
static int vbe_switch(uint32_t width, uint32_t height, uint64_t *physical, uint32_t *pitch)
{
    static vbe_mode_info_t modes[VBE_MODES_MAX];
    int count = vbe_list_modes(modes, VBE_MODES_MAX);

    for (int i = 0; i < count; i++)
    {
        if (modes[i].width == width && modes[i].height == height && vbe_set_mode(&modes[i]) == 0)
        {
            *physical = modes[i].framebuffer;
            *pitch = modes[i].pitch;
            return 0;
        }
    }
    return -1;
}

int hal_framebuffer_set_mode(uint32_t width, uint32_t height)
{
    uint32_t bga_physical;
    uint64_t physical;
    uint32_t pitch = width * 4;

    if (bga_allowed() && bga_set_mode(width, height, &bga_physical) == 0)
        physical = bga_physical;
    else if (vbe_switch(width, height, &physical, &pitch) != 0)
        return -1;

    size_t bytes = (size_t)pitch * height;
    void *mapped = vmap(physical, bytes, HAL_MMU_WRITABLE | HAL_MMU_WRITE_COMBINING);

    if (mapped == NULL)
        return -1;

    framebuffer.base = mapped;
    framebuffer.width = width;
    framebuffer.height = height;
    framebuffer.pitch = pitch;
    framebuffer.bpp = 32;
    framebuffer_mapped = true;

    return 0;
}

int hal_framebuffer_modes(hal_video_mode_t *modes, int max)
{
    static vbe_mode_info_t list[VBE_MODES_MAX];
    int count = vbe_list_modes(list, VBE_MODES_MAX);

    if (count < 0)
        return -1;

    for (int i = 0; i < count && i < max; i++)
    {
        modes[i].width = list[i].width;
        modes[i].height = list[i].height;
    }
    return count < max ? count : max;
}

int hal_framebuffer_native(uint32_t *width, uint32_t *height)
{
    uint8_t edid[EDID_SIZE];

    if (vbe_read_edid(edid) != 0)
        return -1;
    return edid_preferred(edid, width, height);
}
