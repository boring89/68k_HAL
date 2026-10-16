/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>
#include <string.h>

#include "kernel/block/block.h"
#include "kernel/block/ramdisk.h"

static uint8_t image[4 * RAMDISK_BLOCK_SIZE + 100];

static void reset(void)
{
    block_reset();
    ramdisk_reset();

    for (size_t i = 0; i < sizeof(image); i++)
        image[i] = (uint8_t)(i / RAMDISK_BLOCK_SIZE * 16 + i % 7);
}

static void test_ramdisk_geometry(void)
{
    reset();

    TEST_ASSERT_EQ_INT(0, block_device_count());
    TEST_ASSERT_EQ_INT(0, ramdisk_register("ram0", image, sizeof(image)));
    TEST_ASSERT_EQ_INT(1, block_device_count());

    block_device_t *dev = block_device_get(0);

    TEST_ASSERT(dev != NULL);
    TEST_ASSERT(block_device_find("ram0") == dev);
    TEST_ASSERT(block_device_find("nope") == NULL);
    TEST_ASSERT(block_device_find(NULL) == NULL);
    TEST_ASSERT(block_device_get(1) == NULL);
    TEST_ASSERT_EQ_INT(512, dev->block_size);
    /* The 100 bytes after the 4 blocks are a partial block: ignored. */
    TEST_ASSERT_EQ_INT(4, dev->block_count);
}

static void test_read(void)
{
    reset();
    ramdisk_register("ram0", image, sizeof(image));

    block_device_t *dev = block_device_get(0);
    uint8_t buffer[3 * RAMDISK_BLOCK_SIZE];

    TEST_ASSERT_EQ_INT(0, block_read(dev, 1, 2, buffer));
    TEST_ASSERT(memcmp(buffer, image + 512, 2 * 512) == 0);

    TEST_ASSERT_EQ_INT(0, block_read(dev, 3, 1, buffer));
    TEST_ASSERT(memcmp(buffer, image + 3 * 512, 512) == 0);

    TEST_ASSERT_EQ_INT(0, block_read(dev, 0, 3, buffer));
    TEST_ASSERT(memcmp(buffer, image, 3 * 512) == 0);
}

static void test_read_rejects(void)
{
    reset();
    ramdisk_register("ram0", image, sizeof(image));

    block_device_t *dev = block_device_get(0);
    uint8_t buffer[RAMDISK_BLOCK_SIZE];

    TEST_ASSERT_EQ_INT(-1, block_read(dev, 4, 1, buffer));      /* past the end */
    TEST_ASSERT_EQ_INT(-1, block_read(dev, 3, 2, buffer));      /* runs over */
    TEST_ASSERT_EQ_INT(-1, block_read(dev, 0, 5, buffer));
    TEST_ASSERT_EQ_INT(-1, block_read(dev, 0, 0, buffer));      /* nothing to do */
    TEST_ASSERT_EQ_INT(-1, block_read(dev, 0, 1, NULL));
    TEST_ASSERT_EQ_INT(-1, block_read(NULL, 0, 1, buffer));
    /* lba + count would wrap around 64 bits. */
    TEST_ASSERT_EQ_INT(-1, block_read(dev, UINT64_MAX, 2, buffer));
    TEST_ASSERT_EQ_INT(-1, block_read(dev, 1, UINT32_MAX, buffer));
}

static void test_register_rejects(void)
{
    reset();

    TEST_ASSERT_EQ_INT(-1, ramdisk_register("small", image, 511));
    TEST_ASSERT_EQ_INT(-1, ramdisk_register(NULL, image, sizeof(image)));
    TEST_ASSERT_EQ_INT(-1, ramdisk_register("x", NULL, sizeof(image)));
    TEST_ASSERT_EQ_INT(0, block_device_count());

    TEST_ASSERT_EQ_INT(0, ramdisk_register("a", image, sizeof(image)));
    TEST_ASSERT_EQ_INT(-1, ramdisk_register("a", image, sizeof(image)));
    TEST_ASSERT_EQ_INT(1, block_device_count());
    TEST_ASSERT_EQ_INT(-1, block_register(NULL));

    block_device_t bad = {"bad", 0, 10, NULL, NULL};

    TEST_ASSERT_EQ_INT(-1, block_register(&bad));
}

static void test_table_full(void)
{
    static const char *names[] = {"d0", "d1", "d2", "d3", "d4"};

    reset();

    for (int i = 0; i < RAMDISK_MAX; i++)
        TEST_ASSERT_EQ_INT(0, ramdisk_register(names[i], image, sizeof(image)));

    TEST_ASSERT_EQ_INT(-1, ramdisk_register(names[4], image, sizeof(image)));
    TEST_ASSERT_EQ_INT(RAMDISK_MAX, block_device_count());
}

/* A writable device on a plain array, to test block_write's rules. */
static uint8_t store[4 * 512];

static int fake_read(block_device_t *d, uint64_t lba, uint32_t count, void *buffer)
{
    (void)d;
    memcpy(buffer, store + lba * 512, (size_t)count * 512);

    return 0;
}

static int fake_write(block_device_t *d, uint64_t lba, uint32_t count, const void *buffer)
{
    (void)d;
    memcpy(store + lba * 512, buffer, (size_t)count * 512);

    return 0;
}

static const block_ops_t fake_ops = {fake_read, fake_write};
static const block_ops_t read_only_ops = {fake_read, NULL};

static void test_write(void)
{
    block_device_t rw = {"disk0", 512, 4, &fake_ops, NULL};
    block_device_t ro = {"disk1", 512, 4, &read_only_ops, NULL};
    uint8_t data[2 * 512];
    uint8_t back[2 * 512];

    block_reset();
    TEST_ASSERT_EQ_INT(0, block_register(&rw));
    TEST_ASSERT_EQ_INT(0, block_register(&ro));

    memset(store, 0, sizeof(store));
    memset(data, 0xA5, sizeof(data));

    TEST_ASSERT_EQ_INT(0, block_write(&rw, 1, 2, data));
    TEST_ASSERT_EQ_INT(0, block_read(&rw, 1, 2, back));
    TEST_ASSERT(memcmp(data, back, sizeof(data)) == 0);
    TEST_ASSERT_EQ_INT(0, store[511]);
    TEST_ASSERT_EQ_INT(0xA5, store[512]);
    TEST_ASSERT_EQ_INT(0, store[3 * 512]);

    /* Out of range, empty, no buffer, read-only device: all refused. */
    TEST_ASSERT_EQ_INT(-1, block_write(&rw, 3, 2, data));
    TEST_ASSERT_EQ_INT(-1, block_write(&rw, 4, 1, data));
    TEST_ASSERT_EQ_INT(-1, block_write(&rw, 0, 0, data));
    TEST_ASSERT_EQ_INT(-1, block_write(&rw, 0, 1, NULL));
    TEST_ASSERT_EQ_INT(-1, block_write(NULL, 0, 1, data));
    TEST_ASSERT_EQ_INT(-1, block_write(&ro, 0, 1, data));
}

int main(void)
{
    TEST_RUN(test_ramdisk_geometry);
    TEST_RUN(test_read);
    TEST_RUN(test_read_rejects);
    TEST_RUN(test_register_rejects);
    TEST_RUN(test_table_full);
    TEST_RUN(test_write);

    return TEST_REPORT("block");
}
