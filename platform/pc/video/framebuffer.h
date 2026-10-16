/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_VIDEO_FRAMEBUFFER_H
#define PLATFORM_PC_VIDEO_FRAMEBUFFER_H

#include <stdbool.h>

/*
 * True if the boot loader set up a usable graphics framebuffer (Multiboot
 * info flags bit 12, direct RGB, 32 bpp, below 4 GiB). multiboot_init must
 * have been called.
 */
bool pc_framebuffer_detect(void);

/*
 * Map the framebuffer writable at its physical address. Call after paging
 * is on; until it succeeds hal_framebuffer_get fails. Returns 0, or -1 if
 * there is no framebuffer or a page could not be mapped.
 */
int pc_framebuffer_map(void);

#endif
