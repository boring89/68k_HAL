/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "platform/pc/video/vbe_parse.h"

#include <string.h>

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void test_info_block(void)
{
    uint8_t block[VBE_INFO_SIZE];
    uint16_t version, segment, offset;
    uint32_t kib;

    memset(block, 0, sizeof block);
    TEST_ASSERT_EQ_INT(-1, vbe_parse_info(block, &version, &segment, &offset, &kib));

    memcpy(block, "VESA", 4);
    put16(block + 4, 0x0300);
    put16(block + 14, 0x1234);
    put16(block + 16, 0xC000);
    put16(block + 18, 256);
    TEST_ASSERT_EQ_INT(0, vbe_parse_info(block, &version, &segment, &offset, &kib));
    TEST_ASSERT_EQ_INT(0x0300, version);
    TEST_ASSERT_EQ_INT(0xC000, segment);
    TEST_ASSERT_EQ_INT(0x1234, offset);
    TEST_ASSERT_EQ_INT(16384, (int)kib);
}

static void make_mode(uint8_t *block, uint16_t w, uint16_t h)
{
    memset(block, 0, VBE_MODE_INFO_SIZE);
    put16(block, 0x009B);                   /* supported, color, graphics, linear */
    put16(block + 0x10, w * 4);
    put16(block + 0x12, w);
    put16(block + 0x14, h);
    block[0x19] = 32;
    block[0x1B] = 6;
    block[0x1F] = 8; block[0x20] = 16; block[0x21] = 8; block[0x22] = 8; block[0x23] = 8; block[0x24] = 0;
    block[0x28] = 0x00; block[0x29] = 0x00; block[0x2A] = 0x00; block[0x2B] = 0xFD;
}

static void test_mode_info(void)
{
    uint8_t block[VBE_MODE_INFO_SIZE];
    vbe_mode_info_t info;

    make_mode(block, 1024, 768);
    vbe_parse_mode_info(block, 0x118, 0x0200, &info);
    TEST_ASSERT_EQ_INT(1024, info.width);
    TEST_ASSERT_EQ_INT(768, info.height);
    TEST_ASSERT_EQ_INT(4096, (int)info.pitch);
    TEST_ASSERT_EQ_INT((int)0xFD000000u, (int)info.framebuffer);
    TEST_ASSERT(vbe_mode_usable(&info));

    /* VBE 3.0: the linear mode's pitch and masks are separate fields (empty here: the masks are 0). */
    put16(block + 0x32, 4352);
    vbe_parse_mode_info(block, 0x118, 0x0300, &info);
    TEST_ASSERT_EQ_INT(4352, (int)info.pitch);
    TEST_ASSERT(!vbe_mode_usable(&info));
    block[0x36] = 8; block[0x37] = 16; block[0x38] = 8; block[0x39] = 8; block[0x3A] = 8; block[0x3B] = 0;
    vbe_parse_mode_info(block, 0x118, 0x0300, &info);
    TEST_ASSERT(vbe_mode_usable(&info));

    /* No linear framebuffer, or 16 bits per pixel: not for us. */
    put16(block, 0x001B);
    vbe_parse_mode_info(block, 0x118, 0x0300, &info);
    TEST_ASSERT(!vbe_mode_usable(&info));
    make_mode(block, 800, 600);
    block[0x19] = 16;
    vbe_parse_mode_info(block, 0x114, 0x0200, &info);
    TEST_ASSERT(!vbe_mode_usable(&info));
}

static void test_edid(void)
{
    uint8_t edid[EDID_SIZE];
    uint32_t w, h;
    uint8_t sum = 0;

    memset(edid, 0, sizeof edid);
    TEST_ASSERT_EQ_INT(-1, edid_preferred(edid, &w, &h));

    /* 2560x1440: 2560 = 0xA00 (low byte 0x00, nibble 0xA), 1440 = 0x5A0 (0xA0, nibble 0x5). */
    memcpy(edid, "\x00\xFF\xFF\xFF\xFF\xFF\xFF\x00", 8);
    edid[0x36] = 0x56;
    edid[0x37] = 0x5E;
    edid[0x38] = 0x00;
    edid[0x3A] = 0xA0;
    edid[0x3B] = 0xA0;
    edid[0x3D] = 0x50;
    for (int i = 0; i < EDID_SIZE - 1; i++)
        sum = (uint8_t)(sum + edid[i]);
    edid[127] = (uint8_t)(0x100 - sum);
    TEST_ASSERT_EQ_INT(0, edid_preferred(edid, &w, &h));
    TEST_ASSERT_EQ_INT(2560, (int)w);
    TEST_ASSERT_EQ_INT(1440, (int)h);

    edid[127]++;
    TEST_ASSERT_EQ_INT(-1, edid_preferred(edid, &w, &h));
}

int main(void)
{
    TEST_RUN(test_info_block);
    TEST_RUN(test_mode_info);
    TEST_RUN(test_edid);
    return TEST_REPORT("vbe_parse");
}
