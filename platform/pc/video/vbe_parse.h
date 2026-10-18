/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PLATFORM_PC_VIDEO_VBE_PARSE_H
#define PLATFORM_PC_VIDEO_VBE_PARSE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The data structures of VESA BIOS Extensions (VBE Core 3.0, chapter 4) and of an EDID block (VESA E-EDID
 * 1.3/1.4), decoded without any hardware: platform/pc/video/vbe.c gets the bytes from the video BIOS,
 * tests/host/test_vbe_parse.c checks the decoding.
 */

#define VBE_INFO_SIZE 512       /* VbeInfoBlock (4F00h) */
#define VBE_MODE_INFO_SIZE 256  /* ModeInfoBlock (4F01h) */
#define EDID_SIZE 128

typedef struct
{
    uint16_t mode;              /* the VBE mode number */
    uint16_t attributes;
    uint16_t width, height;
    uint8_t bpp;
    uint8_t memory_model;       /* 4 = packed pixel, 6 = direct color */
    uint32_t pitch;             /* bytes per row in the linear framebuffer */
    uint32_t framebuffer;       /* physical address of the linear framebuffer */
    uint8_t red_size, red_shift, green_size, green_shift, blue_size, blue_shift;
} vbe_mode_info_t;

/* ModeInfoBlock attribute bits (VBE 3.0, "ModeAttributes"). */
#define VBE_ATTR_SUPPORTED 0x0001
#define VBE_ATTR_GRAPHICS 0x0010
#define VBE_ATTR_LINEAR 0x0080

/* The VbeInfoBlock: signature "VESA", version (0x0300 = 3.0), and where the mode list is (a real-mode
 * segment:offset pointer). Returns 0, or -1 if the signature is wrong. */
int vbe_parse_info(const uint8_t *block, uint16_t *version, uint16_t *list_segment, uint16_t *list_offset,
                   uint32_t *memory_kib);

/* Decode a ModeInfoBlock for mode 'mode'. VBE 3.0 blocks give the linear-mode pitch and masks separately. */
void vbe_parse_mode_info(const uint8_t *block, uint16_t mode, uint16_t version, vbe_mode_info_t *info);

/* A mode the kernel can draw on: supported, graphics, linear framebuffer, 32 bits 0x00RRGGBB. */
bool vbe_mode_usable(const vbe_mode_info_t *info);

/* The preferred (native) resolution of a monitor from its EDID: the first detailed timing descriptor.
 * Returns 0, or -1 if the block's header or checksum is wrong or it has no timing. */
int edid_preferred(const uint8_t *edid, uint32_t *width, uint32_t *height);

#endif
