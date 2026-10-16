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

#endif
