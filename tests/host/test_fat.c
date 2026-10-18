/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>
#include <stdlib.h>

#include "kernel/block/block.h"
#include "kernel/fs/fat.h"
#include "kernel/fs/grubenv.h"

/* Written by tools/make_fat_image.sh (see the Makefile). */
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

static const block_ops_t ops = {mem_read, mem_write};
static block_device_t disk = {"boot", 512, 0, &ops, NULL};

static int load(void)
{
    FILE *f = fopen(IMAGE_PATH, "rb");

    if (f == NULL)
    {
        fprintf(stderr, "  cannot open %s\n", IMAGE_PATH);
        test_failures++;
        return -1;
    }

    fseek(f, 0, SEEK_END);
    image_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    free(image);
    image = malloc(image_size);
    TEST_ASSERT_EQ_INT(image_size, fread(image, 1, image_size, f));
    fclose(f);
    disk.block_count = image_size / 512;
    block_reset();
    TEST_ASSERT_EQ_INT(0, block_register(&disk));
    writes = 0;

    return 0;
}

static void test_find(void)
{
    fat_file_t file;
    char text[64];

    if (load() != 0)
        return;

    TEST_ASSERT_EQ_INT(0, fat_find(&disk, "68k_hal/grubenv", &file));
    TEST_ASSERT_EQ_INT(1024, file.size);
    TEST_ASSERT_EQ_INT(2, file.sector_count);

    /* Names are compared without regard to case. */
    TEST_ASSERT_EQ_INT(0, fat_find(&disk, "68K_HAL/68K_HAL.CFG", &file));
    TEST_ASSERT_EQ_INT(16, file.size);
    TEST_ASSERT_EQ_INT(16, fat_read(&disk, &file, text, sizeof text));
    TEST_ASSERT_EQ_INT(0, memcmp(text, "autostart = mac\n", 16));
    TEST_ASSERT_EQ_INT(-1, fat_read(&disk, &file, text, 8));

    TEST_ASSERT_EQ_INT(-1, fat_find(&disk, "68k_hal/missing", &file));
    TEST_ASSERT_EQ_INT(-1, fat_find(&disk, "nothere/grubenv", &file));
    TEST_ASSERT_EQ_INT(-1, fat_find(&disk, "68k_hal", &file));            /* a folder */
    TEST_ASSERT_EQ_INT(-1, fat_find(&disk, "68k_hal/toolongname", &file)); /* not 8.3 */

    /* Not FAT32 at all. */
    image[510] = 0;
    TEST_ASSERT_EQ_INT(-1, fat_find(&disk, "68k_hal/grubenv", &file));
}

static void test_grubenv_rewrite(void)
{
    fat_file_t file;
    char block[GRUBENV_SIZE];
    grubenv_t env;

    if (load() != 0)
        return;

    TEST_ASSERT_EQ_INT(0, fat_find(&disk, "68k_hal/grubenv", &file));
    TEST_ASSERT_EQ_INT(GRUBENV_SIZE, fat_read(&disk, &file, block, sizeof block));

    /* What grub-editenv wrote. */
    TEST_ASSERT_EQ_INT(0, grubenv_parse(&env, block, sizeof block));
    TEST_ASSERT_EQ_INT(3, env.count);
    TEST_ASSERT_EQ_STR("a", grubenv_get(&env, "boot_slot"));
    TEST_ASSERT_EQ_STR("a", grubenv_get(&env, "system_slot"));
    TEST_ASSERT_EQ_STR("", grubenv_get(&env, "trial"));
    TEST_ASSERT(grubenv_get(&env, "missing") == NULL);

    /* Change one, write the file in place, read it back. */
    TEST_ASSERT_EQ_INT(0, grubenv_set(&env, "boot_slot", "b"));
    TEST_ASSERT_EQ_INT(0, grubenv_set(&env, "new", "1"));
    TEST_ASSERT_EQ_INT(0, grubenv_format(&env, block));
    TEST_ASSERT_EQ_INT('#', block[GRUBENV_SIZE - 1]);
    TEST_ASSERT_EQ_INT(0, fat_overwrite(&disk, &file, block));
    TEST_ASSERT_EQ_INT(2, writes);

    memset(block, 0, sizeof block);
    TEST_ASSERT_EQ_INT(0, fat_find(&disk, "68k_hal/grubenv", &file));
    TEST_ASSERT_EQ_INT(1024, file.size);
    TEST_ASSERT_EQ_INT(GRUBENV_SIZE, fat_read(&disk, &file, block, sizeof block));
    TEST_ASSERT_EQ_INT(0, grubenv_parse(&env, block, sizeof block));
    TEST_ASSERT_EQ_STR("b", grubenv_get(&env, "boot_slot"));
    TEST_ASSERT_EQ_STR("1", grubenv_get(&env, "new"));
    TEST_ASSERT_EQ_STR("a", grubenv_get(&env, "system_slot"));

    /* The other file is untouched. */
    char text[16];

    TEST_ASSERT_EQ_INT(0, fat_find(&disk, "68k_hal/68k_hal.cfg", &file));
    TEST_ASSERT_EQ_INT(16, fat_read(&disk, &file, text, sizeof text));
    TEST_ASSERT_EQ_INT(0, memcmp(text, "autostart = mac\n", 16));
}

static void test_grubenv_limits(void)
{
    grubenv_t env;
    char block[GRUBENV_SIZE];

    memset(block, '#', sizeof block);
    TEST_ASSERT_EQ_INT(-1, grubenv_parse(&env, block, sizeof block));  /* no signature */
    TEST_ASSERT_EQ_INT(-1, grubenv_parse(&env, block, 512));

    env.count = 0;
    TEST_ASSERT_EQ_INT(-1, grubenv_set(&env, "a", "two\nlines"));
    TEST_ASSERT_EQ_INT(-1, grubenv_set(&env, "a", "back\\slash"));
    TEST_ASSERT_EQ_INT(-1, grubenv_set(&env, "", "x"));

    /* Too much to fit in 1024 bytes. */
    char value[GRUBENV_VALUE_SIZE];

    memset(value, 'v', sizeof value - 1);
    value[sizeof value - 1] = '\0';
    for (int i = 0; i < GRUBENV_MAX_VARS; i++)
    {
        char name[GRUBENV_NAME_SIZE];

        snprintf(name, sizeof name, "variable_number_%d", i);
        TEST_ASSERT_EQ_INT(0, grubenv_set(&env, name, value));
    }
    TEST_ASSERT_EQ_INT(-1, grubenv_set(&env, "one_more", "x"));
    TEST_ASSERT_EQ_INT(-1, grubenv_format(&env, block));
}

int main(void)
{
    TEST_RUN(test_find);
    TEST_RUN(test_grubenv_rewrite);
    TEST_RUN(test_grubenv_limits);

    free(image);
    return TEST_REPORT("fat");
}
