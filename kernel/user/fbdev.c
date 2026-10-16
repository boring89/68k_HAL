/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "fbdev.h"

#include "process.h"

#include "hal/framebuffer.h"
#include "hal/mmu.h"

#include "kernel/display/fbcon.h"
#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/terminal/terminal.h"

#include <stdbool.h>

/* linux/fb.h */
#define FBIOGET_VSCREENINFO 0x4600u
#define FBIOPUT_VSCREENINFO 0x4601u
#define FBIOGET_FSCREENINFO 0x4602u
#define FBIOPAN_DISPLAY 0x4606u
#define FBIOBLANK 0x4611u

#define FB_TYPE_PACKED_PIXELS 0
#define FB_VISUAL_TRUECOLOR 2

#define FIX_SIZE 68
#define VAR_SIZE 160

/* Set when a program used the framebuffer: the console must be drawn again afterwards. */
static bool used;

int fbdev_available(void)
{
    hal_framebuffer_t fb;

    return hal_framebuffer_get(&fb) == 0;
}

static void put32(uint8_t *block, unsigned int offset, uint32_t value)
{
    k_memcpy(block + offset, &value, 4);
}

static uint32_t get32(const uint8_t *block, unsigned int offset)
{
    uint32_t value;

    k_memcpy(&value, block + offset, 4);

    return value;
}

/* struct fb_var_screeninfo: sizes, then the colour layout of 0x00RRGGBB. */
static void fill_var(uint8_t *block, const hal_framebuffer_t *fb)
{
    k_memset(block, 0, VAR_SIZE);
    put32(block, 0, fb->width);   /* xres */
    put32(block, 4, fb->height);  /* yres */
    put32(block, 8, fb->width);   /* xres_virtual */
    put32(block, 12, fb->height); /* yres_virtual */
    put32(block, 24, 32);         /* bits_per_pixel */
    put32(block, 32, 16);         /* red: offset */
    put32(block, 36, 8);          /*      length */
    put32(block, 44, 8);          /* green */
    put32(block, 48, 8);
    put32(block, 56, 0);          /* blue */
    put32(block, 60, 8);
    put32(block, 80, 0);          /* nonstd */
    put32(block, 84, 0);          /* activate: FB_ACTIVATE_NOW */
    put32(block, 88, 0xFFFFFFFFu); /* height in mm: unknown */
    put32(block, 92, 0xFFFFFFFFu); /* width in mm */
}

/* struct fb_fix_screeninfo */
static void fill_fix(uint8_t *block, const hal_framebuffer_t *fb)
{
    uintptr_t phys = 0;

    hal_mmu_get_phys((uintptr_t)fb->base, &phys);

    k_memset(block, 0, FIX_SIZE);
    k_memcpy(block, "68k_HAL fb", 11);                  /* id[16] */
    put32(block, 16, (uint32_t)phys);                   /* smem_start */
    put32(block, 20, fb->pitch * fb->height);           /* smem_len */
    put32(block, 24, FB_TYPE_PACKED_PIXELS);            /* type */
    put32(block, 32, FB_VISUAL_TRUECOLOR);              /* visual */
    put32(block, 44, fb->pitch);                        /* line_length */
}

static int32_t fbdev_ioctl(vfs_file_t *file, uint32_t request, uint32_t arg)
{
    hal_framebuffer_t fb;
    uint8_t block[VAR_SIZE];

    (void)file;

    if (hal_framebuffer_get(&fb) != 0)
        return -K_ENODEV;

    switch (request)
    {
    case FBIOGET_VSCREENINFO:
        fill_var(block, &fb);

        if (!user_range_writable(arg, VAR_SIZE))
            return -K_EFAULT;

        k_memcpy((void *)(uintptr_t)arg, block, VAR_SIZE);

        return 0;

    case FBIOGET_FSCREENINFO:
        fill_fix(block, &fb);

        if (!user_range_writable(arg, FIX_SIZE))
            return -K_EFAULT;

        k_memcpy((void *)(uintptr_t)arg, block, FIX_SIZE);

        return 0;

    case FBIOPUT_VSCREENINFO:
    {
        if (!user_range_ok(arg, VAR_SIZE))
            return -K_EFAULT;

        k_memcpy(block, (const void *)(uintptr_t)arg, VAR_SIZE);

        uint32_t width = get32(block, 0);
        uint32_t height = get32(block, 4);

        if (get32(block, 24) != 32)
            return -K_EINVAL;

        used = true;

        /* The same mode is fine; another one needs the display adapter to switch. */
        if (width != fb.width || height != fb.height)
        {
            if (hal_framebuffer_set_mode(width, height) != 0)
                return -K_EINVAL;
        }

        return 0;
    }

    case FBIOPAN_DISPLAY:
    case FBIOBLANK:
        return 0;

    default:
        return -K_ENOTTY;
    }
}

/* The framebuffer as a file: read and write at an offset. */
static int32_t clip(uint64_t pos, uint32_t count, const hal_framebuffer_t *fb)
{
    uint64_t size = (uint64_t)fb->pitch * fb->height;

    if (pos >= size)
        return 0;

    return (int32_t)(count < size - pos ? count : size - pos);
}

static int32_t fbdev_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    hal_framebuffer_t fb;

    (void)file;

    if (hal_framebuffer_get(&fb) != 0)
        return -K_ENODEV;

    int32_t n = clip(pos, count, &fb);

    k_memcpy(buffer, fb.base + pos, (size_t)n);

    return n;
}

static int32_t fbdev_write(vfs_file_t *file, uint64_t pos, const void *buffer, uint32_t count)
{
    hal_framebuffer_t fb;

    (void)file;

    if (hal_framebuffer_get(&fb) != 0)
        return -K_ENODEV;

    int32_t n = clip(pos, count, &fb);

    if (n == 0 && count > 0)
        return -K_ENOSPC;

    used = true;
    k_memcpy(fb.base + pos, buffer, (size_t)n);

    return n;
}

static int32_t fbdev_stat(vfs_file_t *file, vfs_stat_t *stat)
{
    hal_framebuffer_t fb;

    (void)file;
    stat->mode = VFS_S_IFCHR | 0666;
    stat->size = hal_framebuffer_get(&fb) == 0 ? (uint64_t)fb.pitch * fb.height : 0;
    stat->inode = 4;
    stat->mtime = 0;

    return 0;
}

static int32_t fbdev_mmap(vfs_file_t *file, uint32_t length, uint32_t offset, uintptr_t *phys)
{
    hal_framebuffer_t fb;

    (void)file;

    if (hal_framebuffer_get(&fb) != 0)
        return -K_ENODEV;

    uint64_t size = (uint64_t)fb.pitch * fb.height;

    if (offset >= size || length > size - offset)
        return -K_EINVAL;

    uintptr_t base;

    if (hal_mmu_get_phys((uintptr_t)fb.base, &base) != 0)
        return -K_ENODEV;

    used = true;
    *phys = (base & ~(uintptr_t)(HAL_MMU_PAGE_SIZE - 1)) + offset;

    return 0;
}

const vfs_file_ops_t fbdev_ops = {
    .read = fbdev_read,
    .write = fbdev_write,
    .stat = fbdev_stat,
    .ioctl = fbdev_ioctl,
    .mmap = fbdev_mmap,
};

void fbdev_program_ended(void)
{
    hal_framebuffer_t fb;

    if (!used)
        return;

    used = false;

    /* The program may have changed the mode: the console follows the screen. */
    if (fbcon_display() != NULL && hal_framebuffer_get(&fb) == 0 && fbcon_init(&fb, 0) == 0)
        terminal_init();
}
