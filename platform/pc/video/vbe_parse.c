/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "vbe_parse.h"

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int vbe_parse_info(const uint8_t *block, uint16_t *version, uint16_t *list_segment, uint16_t *list_offset,
                   uint32_t *memory_kib)
{
    /* VbeSignature, VbeVersion (+4), OemStringPtr (+6), Capabilities (+10), VideoModePtr (+14),
     * TotalMemory in 64 KiB units (+18). */
    if (block[0] != 'V' || block[1] != 'E' || block[2] != 'S' || block[3] != 'A')
        return -1;

    *version = le16(block + 4);
    *list_offset = le16(block + 14);
    *list_segment = le16(block + 16);
    *memory_kib = (uint32_t)le16(block + 18) * 64;
    return 0;
}

void vbe_parse_mode_info(const uint8_t *block, uint16_t mode, uint16_t version, vbe_mode_info_t *info)
{
    /* ModeAttributes +0, BytesPerScanLine +0x10, XResolution +0x12, YResolution +0x14, BitsPerPixel +0x19,
     * MemoryModel +0x1B, Red/Green/Blue MaskSize and FieldPosition +0x1F..+0x24, PhysBasePtr +0x28,
     * LinBytesPerScanLine +0x32, LinRed/Green/Blue... +0x36..+0x3B (VBE 3.0). */
    bool v3 = version >= 0x0300;
    const uint8_t *masks = v3 ? block + 0x36 : block + 0x1F;

    info->mode = mode;
    info->attributes = le16(block);
    info->width = le16(block + 0x12);
    info->height = le16(block + 0x14);
    info->bpp = block[0x19];
    info->memory_model = block[0x1B];
    info->pitch = v3 && le16(block + 0x32) != 0 ? le16(block + 0x32) : le16(block + 0x10);
    info->framebuffer = le32(block + 0x28);
    info->red_size = masks[0];
    info->red_shift = masks[1];
    info->green_size = masks[2];
    info->green_shift = masks[3];
    info->blue_size = masks[4];
    info->blue_shift = masks[5];
}

bool vbe_mode_usable(const vbe_mode_info_t *info)
{
    uint16_t need = VBE_ATTR_SUPPORTED | VBE_ATTR_GRAPHICS | VBE_ATTR_LINEAR;

    return (info->attributes & need) == need && info->bpp == 32 && info->memory_model == 6 &&
           info->framebuffer != 0 && info->red_shift == 16 && info->green_shift == 8 && info->blue_shift == 0 &&
           info->width != 0 && info->height != 0 && info->pitch >= (uint32_t)info->width * 4;
}

int edid_preferred(const uint8_t *edid, uint32_t *width, uint32_t *height)
{
    static const uint8_t header[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    uint8_t sum = 0;

    for (int i = 0; i < 8; i++)
    {
        if (edid[i] != header[i])
            return -1;
    }
    for (int i = 0; i < EDID_SIZE; i++)
        sum = (uint8_t)(sum + edid[i]);
    if (sum != 0)
        return -1;

    /* The first 18-byte descriptor (+0x36) is the preferred timing when its pixel clock is not 0:
     * horizontal active = byte 2 + high nibble of byte 4, vertical active = byte 5 + high nibble of byte 7. */
    const uint8_t *d = edid + 0x36;

    if (le16(d) == 0)
        return -1;
    *width = (uint32_t)d[2] | (uint32_t)(d[4] & 0xF0) << 4;
    *height = (uint32_t)d[5] | (uint32_t)(d[7] & 0xF0) << 4;
    return 0;
}
