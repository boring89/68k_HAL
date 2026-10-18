/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Stand-in for <stdlib.h> for third_party/x86emu. */

#ifndef PLATFORM_PC_VIDEO_X86EMU_INCLUDE_STDLIB_H
#define PLATFORM_PC_VIDEO_X86EMU_INCLUDE_STDLIB_H

#include <stddef.h>

static inline int abs(int x)
{
    return x < 0 ? -x : x;
}

void exit(int status) __attribute__((noreturn));

#endif
