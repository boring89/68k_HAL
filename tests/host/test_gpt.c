/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>

#include "kernel/block/block.h"
#include "kernel/block/ramdisk.h"
#include "kernel/fs/gpt.h"
#include "kernel/lib/k_crc32.h"

/* Written by tools/make_gpt_image.py (see the Makefile). */
#define IMAGE_PATH "build/images/gpt.img"
#define IMAGE_BLOCKS 256

static uint8_t image[IMAGE_BLOCKS * 512];

static block_device_t *load_image(void)
{
    FILE *f = fopen(IMAGE_PATH, "rb");

    block_reset();
    ramdisk_reset();

    if (f == NULL)
    {
        fprintf(stderr, "  cannot open %s\n", IMAGE_PATH);
        test_failures++;
        return NULL;
    }

    size_t got = fread(image, 1, sizeof(image), f);

    fclose(f);
    TEST_ASSERT_EQ_INT(sizeof(image), got);
    TEST_ASSERT_EQ_INT(0, ramdisk_register("img", image, sizeof(image)));

    return block_device_find("img");
}

static void test_crc32(void)
{
    /* The standard check value of CRC-32. */
    TEST_ASSERT_EQ_INT(0xCBF43926u, k_crc32(0, "123456789", 9));
    TEST_ASSERT_EQ_INT(k_crc32(0, "123456789", 9),
                       k_crc32(k_crc32(0, "1234", 4), "56789", 5));
    TEST_ASSERT_EQ_INT(0, k_crc32(0, "", 0));
}

static void test_guid(void)
{
    gpt_guid_t g;

    TEST_ASSERT_EQ_INT(0, gpt_guid_parse(GPT_TYPE_APPLE_HFS, &g));
    /* Apple HFS: first field is stored little-endian. */
    TEST_ASSERT_EQ_INT(0x00, g.b[0]);
    TEST_ASSERT_EQ_INT(0x53, g.b[1]);
    TEST_ASSERT_EQ_INT(0x46, g.b[2]);
    TEST_ASSERT_EQ_INT(0x48, g.b[3]);
    TEST_ASSERT_EQ_INT(0xAA, g.b[6] == 0xAA ? 0xAA : 0);
    TEST_ASSERT_EQ_INT(0xAC, g.b[15]);
    TEST_ASSERT_EQ_INT(-1, gpt_guid_parse("48465300-0000-11AA-AA11", &g));
    TEST_ASSERT_EQ_INT(-1, gpt_guid_parse("4846530X-0000-11AA-AA11-00306543ECAC", &g));
    TEST_ASSERT_EQ_INT(-1, gpt_guid_parse(NULL, &g));
}

static void test_read(void)
{
    block_device_t *dev = load_image();
    gpt_partition_t p[GPT_MAX_PARTITIONS];

    if (dev == NULL)
        return;

    TEST_ASSERT_EQ_INT(5, gpt_read(dev, p, GPT_MAX_PARTITIONS));
    TEST_ASSERT_EQ_STR("68k_HAL Boot", p[0].name);
    TEST_ASSERT_EQ_INT(34, p[0].start);
    TEST_ASSERT_EQ_INT(30, p[0].block_count);
    TEST_ASSERT_EQ_STR("Applications", p[4].name);
    TEST_ASSERT_EQ_INT(28, p[4].block_count);

    /* A smaller table is cut at max. */
    TEST_ASSERT_EQ_INT(2, gpt_read(dev, p, 2));
}

static void test_find(void)
{
    block_device_t *dev = load_image();
    gpt_partition_t p;

    if (dev == NULL)
        return;

    TEST_ASSERT_EQ_INT(0, gpt_find_by_name(dev, GPT_NAME_OVERLAY, &p));
    TEST_ASSERT_EQ_INT(144, p.start);
    TEST_ASSERT_EQ_INT(50, p.block_count);

    TEST_ASSERT_EQ_INT(0, gpt_find_by_name(dev, GPT_NAME_SYSTEM_B, &p));
    TEST_ASSERT_EQ_INT(104, p.start);

    TEST_ASSERT_EQ_INT(-1, gpt_find_by_name(dev, "nope", &p));

    /* The first partition with the Apple HFS type is System A. */
    TEST_ASSERT_EQ_INT(0, gpt_find_by_type(dev, GPT_TYPE_APPLE_HFS, &p));
    TEST_ASSERT_EQ_STR(GPT_NAME_SYSTEM_A, p.name);
    TEST_ASSERT_EQ_INT(0, gpt_find_by_type(dev, GPT_TYPE_OVERLAY, &p));
    TEST_ASSERT_EQ_STR(GPT_NAME_OVERLAY, p.name);
}

static void test_damage(void)
{
    block_device_t *dev = load_image();
    gpt_partition_t p[GPT_MAX_PARTITIONS];

    if (dev == NULL)
        return;

    /* A flipped byte in the entry array breaks its CRC. */
    image[2 * 512 + 56] ^= 0x01;
    TEST_ASSERT_EQ_INT(-1, gpt_read(dev, p, GPT_MAX_PARTITIONS));
    image[2 * 512 + 56] ^= 0x01;
    TEST_ASSERT_EQ_INT(5, gpt_read(dev, p, GPT_MAX_PARTITIONS));

    /* A flipped byte in the header breaks the header CRC. */
    image[512 + 40] ^= 0x01;
    TEST_ASSERT_EQ_INT(-1, gpt_read(dev, p, GPT_MAX_PARTITIONS));
    image[512 + 40] ^= 0x01;

    /* No signature: not an error, just no GPT. */
    image[512] = 'X';
    TEST_ASSERT_EQ_INT(0, gpt_read(dev, p, GPT_MAX_PARTITIONS));
}

static gpt_view_t view;

static void test_view(void)
{
    block_device_t *dev = load_image();
    gpt_partition_t p;
    uint8_t buf[512];

    if (dev == NULL)
        return;

    TEST_ASSERT_EQ_INT(0, gpt_find_by_name(dev, GPT_NAME_SYSTEM_A, &p));
    TEST_ASSERT_EQ_INT(0, gpt_view_register(&view, "sysa", dev, p.start,
                                            p.block_count));

    block_device_t *v = block_device_find("sysa");

    TEST_ASSERT(v != NULL);
    TEST_ASSERT_EQ_INT(40, v->block_count);
    TEST_ASSERT_EQ_INT(0, block_read(v, 0, 1, buf));
    TEST_ASSERT_EQ_INT(0, memcmp(buf, "FIRST-BLOCK-OF-68k_HAL System A", 31));
    TEST_ASSERT_EQ_INT(-1, block_read(v, 40, 1, buf));

    /* A view past the end of its parent is refused. */
    static gpt_view_t bad;

    TEST_ASSERT_EQ_INT(-1, gpt_view_register(&bad, "bad", dev, 250, 20));
}

int main(void)
{
    TEST_RUN(test_crc32);
    TEST_RUN(test_guid);
    TEST_RUN(test_read);
    TEST_RUN(test_find);
    TEST_RUN(test_damage);
    TEST_RUN(test_view);

    return TEST_REPORT("gpt");
}
