/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "kernel/block/block.h"
#include "kernel/fs/fat.h"
#include "kernel/mac/pram.h"

/* Written by tools/make_fat_image.sh: 68k_hal/pram is 512 zero bytes. */
#define IMAGE_PATH "build/images/fat.img"

static uint8_t *image;
static size_t image_size;
static int writes;

static int mem_read(block_device_t *d, uint64_t lba, uint32_t count, void *buf)
{
    (void)d;
    memcpy(buf, image + lba * 512, (size_t)count * 512);
    return 0;
}

static int mem_write(block_device_t *d, uint64_t lba, uint32_t count, const void *buf)
{
    (void)d;
    writes += (int)count;
    memcpy(image + lba * 512, buf, (size_t)count * 512);
    return 0;
}

static int fail_write(block_device_t *d, uint64_t lba, uint32_t count, const void *buf)
{
    (void)d;
    (void)lba;
    (void)count;
    (void)buf;
    return -1;
}

static const block_ops_t ops = {mem_read, mem_write};
static const block_ops_t broken_ops = {mem_read, fail_write};
static block_device_t disk = {"boot", 512, 0, &ops, NULL};

static bool load(void)
{
    FILE *f = fopen(IMAGE_PATH, "rb");

    if (f == NULL)
    {
        fprintf(stderr, "  cannot open %s\n", IMAGE_PATH);
        test_failures++;
        return false;
    }
    fseek(f, 0, SEEK_END);
    image_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    free(image);
    image = malloc(image_size);
    TEST_ASSERT_EQ_INT(image_size, fread(image, 1, image_size, f));
    fclose(f);
    disk.block_count = image_size / 512;
    disk.ops = &ops;
    writes = 0;

    return true;
}

static void sample(uint8_t xpram[MAC_PRAM_SIZE], uint8_t seed)
{
    for (int i = 0; i < MAC_PRAM_SIZE; i++)
        xpram[i] = (uint8_t)(i * 3 + seed);
}

static void test_encode_decode(void)
{
    uint8_t xpram[MAC_PRAM_SIZE], back[MAC_PRAM_SIZE], file[MAC_PRAM_FILE_SIZE];

    sample(xpram, 7);
    mac_pram_encode(xpram, file);
    TEST_ASSERT(memcmp(file, "68kHALPR", 8) == 0);
    TEST_ASSERT_EQ_INT(MAC_PRAM_OK, mac_pram_decode(file, back));
    TEST_ASSERT(memcmp(xpram, back, sizeof back) == 0);

    /* All zeros is "empty", and an all-zero parameter RAM is still kept (it is not empty). */
    memset(xpram, 0, sizeof xpram);
    mac_pram_encode(xpram, file);
    memset(back, 0xAA, sizeof back);
    TEST_ASSERT_EQ_INT(MAC_PRAM_OK, mac_pram_decode(file, back));
    TEST_ASSERT_EQ_INT(0, back[0]);
    memset(file, 0, sizeof file);
    memset(back, 0xAA, sizeof back);
    TEST_ASSERT_EQ_INT(MAC_PRAM_EMPTY, mac_pram_decode(file, back));
    TEST_ASSERT_EQ_INT(0xAA, back[0]);

    /* One flipped bit, another version, another magic: damaged, and xpram is left alone. */
    sample(xpram, 1);
    mac_pram_encode(xpram, file);
    file[32 + 100] ^= 0x10;
    TEST_ASSERT_EQ_INT(MAC_PRAM_DAMAGED, mac_pram_decode(file, back));
    TEST_ASSERT_EQ_INT(0xAA, back[0]);
    mac_pram_encode(xpram, file);
    file[8] = 2;
    TEST_ASSERT_EQ_INT(MAC_PRAM_DAMAGED, mac_pram_decode(file, back));
    mac_pram_encode(xpram, file);
    file[0] = 'X';
    TEST_ASSERT_EQ_INT(MAC_PRAM_DAMAGED, mac_pram_decode(file, back));
}

static void test_file(void)
{
    uint8_t xpram[MAC_PRAM_SIZE], back[MAC_PRAM_SIZE];

    if (!load())
        return;

    /* The new file is empty. */
    TEST_ASSERT_EQ_INT(MAC_PRAM_EMPTY, mac_pram_load(&disk, back));

    /* Saved, read back; one sector written. */
    sample(xpram, 42);
    TEST_ASSERT_EQ_INT(0, mac_pram_save(&disk, xpram));
    TEST_ASSERT_EQ_INT(1, writes);
    TEST_ASSERT_EQ_INT(MAC_PRAM_OK, mac_pram_load(&disk, back));
    TEST_ASSERT(memcmp(xpram, back, sizeof back) == 0);

    /* The other files are untouched. */
    fat_file_t cfg;
    char text[32] = {0};

    TEST_ASSERT_EQ_INT(0, fat_find(&disk, "68k_hal/68k_hal.cfg", &cfg));
    TEST_ASSERT(fat_read(&disk, &cfg, text, sizeof text - 1) > 0);
    TEST_ASSERT(strncmp(text, "autostart = mac", 15) == 0);

    /* Cleared: empty again. */
    TEST_ASSERT_EQ_INT(0, mac_pram_clear(&disk));
    TEST_ASSERT_EQ_INT(MAC_PRAM_EMPTY, mac_pram_load(&disk, back));

    /* A write error. */
    disk.ops = &broken_ops;
    TEST_ASSERT_EQ_INT(-1, mac_pram_save(&disk, xpram));
    disk.ops = &ops;
}

static void test_no_file(void)
{
    uint8_t xpram[MAC_PRAM_SIZE];

    sample(xpram, 0);
    TEST_ASSERT_EQ_INT(MAC_PRAM_NO_FILE, mac_pram_load(NULL, xpram));
    TEST_ASSERT_EQ_INT(-1, mac_pram_save(NULL, xpram));

    if (!load())
        return;

    /* Not FAT at all. */
    memset(image, 0, image_size);
    TEST_ASSERT_EQ_INT(MAC_PRAM_NO_FILE, mac_pram_load(&disk, xpram));
    TEST_ASSERT_EQ_INT(-1, mac_pram_clear(&disk));
    TEST_ASSERT_EQ_INT(0, writes);
}

int main(void)
{
    TEST_RUN(test_encode_decode);
    TEST_RUN(test_file);
    TEST_RUN(test_no_file);
    free(image);

    return TEST_REPORT("pram");
}
