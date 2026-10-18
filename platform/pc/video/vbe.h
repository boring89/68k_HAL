/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_VIDEO_VBE_H
#define PLATFORM_PC_VIDEO_VBE_H

#include <stdint.h>

#include "vbe_parse.h"

/*
 * The video BIOS's VESA BIOS Extensions, run in an emulated CPU (vbe.c). Only on a machine started from a
 * legacy BIOS or a CSM: every function returns -1 when there is no video BIOS (a UEFI boot).
 */

/* 0 if the video BIOS answers VBE calls. */
int vbe_available(void);

/* The modes the kernel can use (32 bits per pixel, linear framebuffer), at most 'max'. Returns the
 * number, or -1. */
int vbe_list_modes(vbe_mode_info_t *modes, int max);

/* Switch to a mode from vbe_list_modes (with its linear framebuffer). Returns 0 or -1. */
int vbe_set_mode(const vbe_mode_info_t *mode);

/* The monitor's EDID block (128 bytes) through VBE/DDC. Returns 0 or -1. */
int vbe_read_edid(uint8_t *edid);

#endif
