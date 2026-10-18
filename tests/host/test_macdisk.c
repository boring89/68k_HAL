/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>

#include "kernel/block/block.h"
#include "kernel/fs/macdisk.h"
#include "kernel/memory/heap.h"

/* Written by tools/make_gpt_image.py (see the Makefile). */
#define IMAGE_PATH "build/images/gpt.img"
#define IMAGE_BLOCKS 256

static uint8_t image[IMAGE_BLOCKS * 512];
static uint8_t arena[256 * 1024] __attribute__((aligned(16)));

static int mem_read(block_device_t *d, uint64_t lba, uint32_t count, void *buf)
{
    memcpy(buf, (uint8_t *)d->driver_data + lba * 512, (size_t)count * 512);
    return 0;
}

static int mem_write(block_device_t *d, uint64_t lba, uint32_t count,
                     const void *buf)
{
    memcpy((uint8_t *)d->driver_data + lba * 512, buf, (size_t)count * 512);
    return 0;
}

static const block_ops_t ops = {mem_read, mem_write};
static block_device_t disk = {"sda", 512, IMAGE_BLOCKS, &ops, image};

static int load_image(void)
{
    FILE *f = fopen(IMAGE_PATH, "rb");

    if (f == NULL)
    {
        fprintf(stderr, "  cannot open %s\n", IMAGE_PATH);
        test_failures++;
        return -1;
    }

    size_t got = fread(image, 1, sizeof(image), f);

    fclose(f);
    TEST_ASSERT_EQ_INT(sizeof(image), got);

    return 0;
}

/* A fresh boot: the block table and macdisk start empty, the disk keeps its content. */
static int boot(uint32_t slot)
{
    block_reset();
    macdisk_forget();
    TEST_ASSERT_EQ_INT(0, block_register(&disk));

    return macdisk_init(slot);
}

static void test_slot_name(void)
{
    TEST_ASSERT_EQ_INT(MACDISK_SLOT_A, macdisk_slot_from_name(NULL));
    TEST_ASSERT_EQ_INT(MACDISK_SLOT_A, macdisk_slot_from_name("a"));
    TEST_ASSERT_EQ_INT(MACDISK_SLOT_B, macdisk_slot_from_name("b"));
    TEST_ASSERT_EQ_INT(MACDISK_SLOT_B, macdisk_slot_from_name("B"));
    TEST_ASSERT_EQ_INT(MACDISK_SLOT_A, macdisk_slot_from_name("bb"));
}

static void test_layout(void)
{
    heap_init((uintptr_t)arena, sizeof(arena));
    if (load_image() != 0)
        return;

    TEST_ASSERT_EQ_INT(0, boot(MACDISK_SLOT_A));
    TEST_ASSERT_EQ_INT(1, macdisk_present());
    TEST_ASSERT_EQ_INT(OVERLAY_OK, macdisk_overlay_status());
    TEST_ASSERT(block_device_find("sys_a") != NULL);
    TEST_ASSERT(block_device_find("sys_b") != NULL);
    TEST_ASSERT(block_device_find("apps") != NULL);
    TEST_ASSERT(block_device_find("boot") != NULL);
    TEST_ASSERT(block_device_find("system") == macdisk_system());
    TEST_ASSERT_EQ_INT(40, macdisk_system()->block_count);
    TEST_ASSERT_EQ_INT(28, macdisk_apps()->block_count);
}

static void test_persistence_and_reset(void)
{
    uint8_t data[512];
    uint8_t back[512];

    heap_init((uintptr_t)arena, sizeof(arena));
    if (load_image() != 0)
        return;

    memset(data, 0x5A, sizeof(data));
    TEST_ASSERT_EQ_INT(0, boot(MACDISK_SLOT_A));
    TEST_ASSERT_EQ_INT(0, block_write(macdisk_system(), 3, 1, data));

    /* System A itself is untouched: the write is in the overlay. */
    TEST_ASSERT(memcmp(image + (64 + 3) * 512, data, 512) != 0);

    /* Reboot: the write is still there. */
    TEST_ASSERT_EQ_INT(0, boot(MACDISK_SLOT_A));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, macdisk_overlay_status());
    TEST_ASSERT_EQ_INT(1, overlay_used_blocks(macdisk_overlay()));
    TEST_ASSERT_EQ_INT(0, block_read(macdisk_system(), 3, 1, back));
    TEST_ASSERT(memcmp(back, data, 512) == 0);

    /* The Applications disk is written directly. */
    TEST_ASSERT_EQ_INT(0, block_write(macdisk_apps(), 1, 1, data));
    TEST_ASSERT(memcmp(image + (194 + 1) * 512, data, 512) == 0);

    /* Reset: the system is back to the image, Applications keep the write. */
    TEST_ASSERT_EQ_INT(0, macdisk_reset());
    TEST_ASSERT_EQ_INT(0, block_read(macdisk_system(), 3, 1, back));
    TEST_ASSERT(memcmp(back, image + (64 + 3) * 512, 512) == 0);
    TEST_ASSERT_EQ_INT(0, block_read(macdisk_apps(), 1, 1, back));
    TEST_ASSERT(memcmp(back, data, 512) == 0);
}

static void test_mismatch_after_image_change(void)
{
    uint8_t data[512];

    heap_init((uintptr_t)arena, sizeof(arena));
    if (load_image() != 0)
        return;

    memset(data, 0x33, sizeof(data));
    TEST_ASSERT_EQ_INT(0, boot(MACDISK_SLOT_A));
    TEST_ASSERT_EQ_INT(0, block_write(macdisk_system(), 5, 1, data));

    /* System A is replaced by another image. */
    image[(64 + 1) * 512] ^= 0xFF;
    TEST_ASSERT_EQ_INT(0, boot(MACDISK_SLOT_A));
    TEST_ASSERT_EQ_INT(OVERLAY_MISMATCH, macdisk_overlay_status());
    TEST_ASSERT(macdisk_system() == NULL);
    TEST_ASSERT(block_device_find("system") == NULL);

    /* Booting System B instead is also a different slot: mismatch. */
    TEST_ASSERT_EQ_INT(0, boot(MACDISK_SLOT_B));
    TEST_ASSERT_EQ_INT(OVERLAY_MISMATCH, macdisk_overlay_status());

    /* Reset binds the overlay to the image as it is. */
    TEST_ASSERT_EQ_INT(0, macdisk_reset());
    TEST_ASSERT(macdisk_system() != NULL);
    TEST_ASSERT_EQ_INT(0, boot(MACDISK_SLOT_B));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, macdisk_overlay_status());
}

static void test_no_layout(void)
{
    uint8_t plain[16 * 512] = {0};
    block_device_t other = {"hd", 512, 16, &ops, plain};

    block_reset();
    macdisk_forget();
    TEST_ASSERT_EQ_INT(0, block_register(&other));
    TEST_ASSERT_EQ_INT(-1, macdisk_init(MACDISK_SLOT_A));
    TEST_ASSERT_EQ_INT(0, macdisk_present());
    TEST_ASSERT_EQ_INT(-1, macdisk_reset());
}

int main(void)
{
    TEST_RUN(test_slot_name);
    TEST_RUN(test_layout);
    TEST_RUN(test_persistence_and_reset);
    TEST_RUN(test_mismatch_after_image_change);
    TEST_RUN(test_no_layout);

    return TEST_REPORT("macdisk");
}
