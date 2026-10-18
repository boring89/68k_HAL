/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Stand-in for X11's Xfuncproto.h: the few macros third_party/x86emu uses. */

#ifndef PLATFORM_PC_VIDEO_X86EMU_INCLUDE_X11_XFUNCPROTO_H
#define PLATFORM_PC_VIDEO_X86EMU_INCLUDE_X11_XFUNCPROTO_H

#define _X_ATTRIBUTE_PRINTF(x, y) __attribute__((format(printf, x, y)))
#define _X_EXPORT
#define _X_HIDDEN
#define _X_INTERNAL
#define _XFUNCPROTOBEGIN
#define _XFUNCPROTOEND
#define _X_NORETURN __attribute__((noreturn))
#define _X_UNUSED __attribute__((unused))

#endif
