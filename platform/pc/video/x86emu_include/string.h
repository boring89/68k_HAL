/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Stand-in for <string.h> for third_party/x86emu (the kernel's memset and memcpy, platform/pc/video/vbe.c). */

#ifndef PLATFORM_PC_VIDEO_X86EMU_INCLUDE_STRING_H
#define PLATFORM_PC_VIDEO_X86EMU_INCLUDE_STRING_H

#include <stddef.h>

void *memset(void *s, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);

#endif
