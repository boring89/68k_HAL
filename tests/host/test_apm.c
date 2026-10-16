/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>

#include "kernel/block/block.h"
#include "kernel/block/ramdisk.h"
#include "kernel/fs/apm.h"

/* Written by tools/make_apm_image.py (see the Makefile). */
#define IMAGE_PATH "build/images/apm.img"
#define IMAGE_BLOCKS 64

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

static void test_read_map(void)
{
    block_device_t *dev = load_image();
    apm_partition_t p[APM_MAX_PARTITIONS];

    if (dev == NULL)
        return;

    TEST_ASSERT_EQ_INT(4, apm_read(dev, p, APM_MAX_PARTITIONS));

    TEST_ASSERT_EQ_INT(0, strcmp(p[0].type, "Apple_partition_map"));
    TEST_ASSERT_EQ_INT(0, strcmp(p[0].name, "Apple"));
    TEST_ASSERT_EQ_INT(1, p[0].start);
    TEST_ASSERT_EQ_INT(4, p[0].block_count);

    TEST_ASSERT_EQ_INT(0, strcmp(p[2].type, "Apple_HFS"));
    TEST_ASSERT_EQ_INT(0, strcmp(p[2].name, "Test volume"));
    TEST_ASSERT_EQ_INT(16, p[2].start);
    TEST_ASSERT_EQ_INT(32, p[2].block_count);
    TEST_ASSERT_EQ_INT(0xB7, p[2].status);

    TEST_ASSERT_EQ_INT(0, strcmp(p[3].type, "Apple_Free"));

    /* A smaller buffer cuts the list. */
    TEST_ASSERT_EQ_INT(2, apm_read(dev, p, 2));
}

static void test_find_hfs(void)
{
    block_device_t *dev = load_image();
    apm_partition_t part;

    if (dev == NULL)
        return;

    TEST_ASSERT_EQ_INT(0, apm_find(dev, APM_TYPE_HFS, &part));
    TEST_ASSERT_EQ_INT(16, part.start);

    /* The partition starts where the image has its marker. */
    TEST_ASSERT(memcmp(image + part.start * 512,
                       "FIRST-BLOCK-OF-THE-HFS-PARTITION", 32) == 0);

    TEST_ASSERT_EQ_INT(-1, apm_find(dev, "Apple_UNIX_SVR2", &part));
    TEST_ASSERT_EQ_INT(-1, apm_find(dev, NULL, &part));
    TEST_ASSERT_EQ_INT(-1, apm_find(dev, APM_TYPE_HFS, NULL));
}

static void test_no_map(void)
{
    block_device_t *dev = load_image();
    apm_partition_t p[4];

    if (dev == NULL)
        return;

    /* Wipe block 1: no "PM" signature means no map, not an error. */
    memset(image + 512, 0, 512);
    TEST_ASSERT_EQ_INT(0, apm_read(dev, p, 4));
    TEST_ASSERT_EQ_INT(-1, apm_find(dev, APM_TYPE_HFS, p));
}

static void test_damaged_map(void)
{
    block_device_t *dev = load_image();
    apm_partition_t p[8];

    if (dev == NULL)
        return;

    /* Entry 3 claims a different map size than entry 1. */
    image[3 * 512 + 7] = 9;
    TEST_ASSERT_EQ_INT(-1, apm_read(dev, p, 8));

    /* A map longer than the disk (pmMapBlkCnt = 1000 in entry 1). */
    load_image();
    image[512 + 6] = 0x03;
    image[512 + 7] = 0xE8;
    TEST_ASSERT_EQ_INT(-1, apm_read(dev, p, 8));

    /* A map size of zero is nonsense too. */
    load_image();
    image[512 + 4] = image[512 + 5] = image[512 + 6] = image[512 + 7] = 0;
    TEST_ASSERT_EQ_INT(-1, apm_read(dev, p, 8));
}

static void test_name_without_nul(void)
{
    uint8_t entry[512] = {0};
    apm_partition_t p;

    entry[0] = 'P';
    entry[1] = 'M';
    entry[7] = 1;
    /* All 32 name bytes used: the copy must still end with a NUL. */
    memset(entry + 16, 'n', 32);
    memcpy(entry + 48, "Apple_HFS", 9);

    TEST_ASSERT_EQ_INT(1, apm_parse_entry(entry, &p));
    TEST_ASSERT_EQ_INT(32, strlen(p.name));
    TEST_ASSERT_EQ_INT(0, strcmp(p.type, "Apple_HFS"));

    entry[0] = 'X';
    TEST_ASSERT_EQ_INT(-1, apm_parse_entry(entry, &p));
}

int main(void)
{
    TEST_RUN(test_read_map);
    TEST_RUN(test_find_hfs);
    TEST_RUN(test_no_map);
    TEST_RUN(test_damaged_map);
    TEST_RUN(test_name_without_nul);

    return TEST_REPORT("apm");
}
