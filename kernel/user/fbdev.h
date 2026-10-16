/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_USER_FBDEV_H
#define KERNEL_USER_FBDEV_H

#include "kernel/vfs/vfs.h"

/*
 * /dev/fb0: the graphics framebuffer for programs, with the Linux
 * framebuffer interface (linux/fb.h). A program opens it, asks for the size
 * (ioctl FBIOGET_VSCREENINFO: xres, yres, 32 bits per pixel, red at bit 16)
 * and the line length (FBIOGET_FSCREENINFO), then mmap()s it (MAP_SHARED) and
 * writes pixels, or uses lseek and write. FBIOPUT_VSCREENINFO with another
 * xres and yres sets that mode where the display adapter allows it
 * (hal_framebuffer_set_mode). Only 32 bpp.
 *
 * The text console is drawn on the same screen; when the program ends the
 * console is drawn again (fbdev_program_ended).
 */

extern const vfs_file_ops_t fbdev_ops;

/* 1 if there is a framebuffer to open. */
int fbdev_available(void);

/* A program ended: if it used /dev/fb0, bring the text console back. */
void fbdev_program_ended(void);

#endif
