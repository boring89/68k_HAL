/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_FRAMEBUFFER_H
#define HAL_FRAMEBUFFER_H

#include <stdint.h>

/*
 * A linear graphics framebuffer set up by the boot loader. Implemented once
 * per platform (platform/<platform>/video/hal_framebuffer.c).
 *
 * Pixels are 32 bits, 0x00RRGGBB (red in bits 16-23): that is what the PC
 * platform accepts, and it ignores any other format.
 */
typedef struct
{
    uint8_t *base;      /* virtual address of the first pixel, mapped writable */
    uint32_t width;     /* pixels */
    uint32_t height;    /* pixels */
    uint32_t pitch;     /* bytes from one row to the next */
    uint32_t bpp;       /* bits per pixel, always 32 */
} hal_framebuffer_t;

/*
 * The framebuffer. Returns 0 and fills *fb, or -1 if the machine booted in
 * text mode, the mode is not supported, it could not be mapped, or fb is
 * NULL. Valid only after hal_init.
 */
int hal_framebuffer_get(hal_framebuffer_t *fb);

/*
 * Change the screen mode to width x height pixels at 32 bpp, for display
 * adapters whose mode can be set directly (on the PC: the Bochs/QEMU VGA).
 * Works whether or not the boot loader set up a graphics mode. On success
 * returns 0 and hal_framebuffer_get describes the new mode (the pixels are
 * not cleared, and pointers from before are no longer valid). Returns -1 if
 * the adapter cannot do it. Every mode change adds to the address space the
 * kernel uses for the frame buffer, so do not switch modes in a loop.
 */
int hal_framebuffer_set_mode(uint32_t width, uint32_t height);

#endif
