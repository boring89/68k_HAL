/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>
#include <stdlib.h>

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

/* With a FAT32 Boot partition (tools/make_gpt_image.py ... fat.img): grubenv, switch and promote. */
#define FAT_IMAGE_PATH "build/images/gpt_fat.img"

static uint8_t *big;
static size_t big_size;
static block_device_t big_disk = {"sdb", 512, 0, &ops, NULL};

static int boot_big(uint32_t slot)
{
    block_reset();
    macdisk_forget();
    big_disk.block_count = big_size / 512;
    big_disk.driver_data = big;
    TEST_ASSERT_EQ_INT(0, block_register(&big_disk));

    return macdisk_init(slot);
}

static int load_big(void)
{
    FILE *f = fopen(FAT_IMAGE_PATH, "rb");

    if (f == NULL)
    {
        fprintf(stderr, "  cannot open %s\n", FAT_IMAGE_PATH);
        test_failures++;
        return -1;
    }

    fseek(f, 0, SEEK_END);
    big_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    free(big);
    big = malloc(big_size);
    TEST_ASSERT_EQ_INT(big_size, fread(big, 1, big_size, f));
    fclose(f);

    return 0;
}

static void test_env_and_confirm(void)
{
    grubenv_t env;

    heap_init((uintptr_t)arena, sizeof(arena));
    if (load_big() != 0)
        return;

    TEST_ASSERT_EQ_INT(0, boot_big(MACDISK_SLOT_A));
    TEST_ASSERT_EQ_INT(0, macdisk_env_read(&env));
    TEST_ASSERT_EQ_STR("a", grubenv_get(&env, "boot_slot"));

    /* No trial boot, or the same slot: nothing to write. */
    TEST_ASSERT_EQ_INT(0, macdisk_confirm_kernel_slot("b", 0));
    TEST_ASSERT_EQ_INT(0, macdisk_confirm_kernel_slot("a", 1));
    TEST_ASSERT_EQ_INT(0, macdisk_confirm_kernel_slot("x", 1));
    TEST_ASSERT_EQ_INT(0, macdisk_confirm_kernel_slot(NULL, 1));

    /* A trial boot of slot b that came up: b is confirmed. */
    TEST_ASSERT_EQ_INT(1, macdisk_confirm_kernel_slot("b", 1));
    TEST_ASSERT_EQ_INT(0, boot_big(MACDISK_SLOT_A));
    TEST_ASSERT_EQ_INT(0, macdisk_env_read(&env));
    TEST_ASSERT_EQ_STR("b", grubenv_get(&env, "boot_slot"));
    TEST_ASSERT_EQ_STR("a", grubenv_get(&env, "system_slot"));

    /* GRUB could not clear trial: the confirmation clears it. */
    TEST_ASSERT_EQ_INT(0, grubenv_set(&env, "trial", "b"));
    TEST_ASSERT_EQ_INT(0, macdisk_env_write(&env));
    TEST_ASSERT_EQ_INT(1, macdisk_confirm_kernel_slot("b", 1));
    TEST_ASSERT_EQ_INT(0, macdisk_env_read(&env));
    TEST_ASSERT_EQ_STR("", grubenv_get(&env, "trial"));
    TEST_ASSERT_EQ_STR("b", grubenv_get(&env, "boot_slot"));
}

static void test_switch_and_promote(void)
{
    uint8_t data[512];
    uint8_t back[512];
    grubenv_t env;

    heap_init((uintptr_t)arena, sizeof(arena));
    if (load_big() != 0)
        return;

    memset(data, 0x6B, sizeof data);
    TEST_ASSERT_EQ_INT(0, boot_big(MACDISK_SLOT_A));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, macdisk_overlay_status());
    TEST_ASSERT_EQ_INT(0, block_write(macdisk_system(), 9, 1, data));

    /* The overlay holds a change: switching needs force. */
    TEST_ASSERT_EQ_INT(MACDISK_OVERLAY_NOT_EMPTY, macdisk_switch(MACDISK_SLOT_B, 0));
    TEST_ASSERT_EQ_INT(MACDISK_SLOT_A, macdisk_slot());

    /* Promote: System B = A plus the change; the overlay is empty, B is active and saved. */
    block_device_t *sys_a = macdisk_slot_device(MACDISK_SLOT_A);
    uint8_t a_before[512];

    TEST_ASSERT_EQ_INT(0, block_read(sys_a, 9, 1, a_before));
    TEST_ASSERT_EQ_INT(0, macdisk_promote(NULL));
    TEST_ASSERT_EQ_INT(MACDISK_SLOT_B, macdisk_slot());
    TEST_ASSERT_EQ_INT(0, overlay_used_blocks(macdisk_overlay()));
    TEST_ASSERT_EQ_INT(0, block_read(macdisk_system(), 9, 1, back));
    TEST_ASSERT(memcmp(back, data, 512) == 0);
    TEST_ASSERT_EQ_INT(0, block_read(macdisk_slot_device(MACDISK_SLOT_B), 9, 1, back));
    TEST_ASSERT(memcmp(back, data, 512) == 0);
    TEST_ASSERT_EQ_INT(0, block_read(sys_a, 9, 1, back));
    TEST_ASSERT(memcmp(back, a_before, 512) == 0);          /* the old slot is untouched */
    TEST_ASSERT_EQ_INT(0, macdisk_env_read(&env));
    TEST_ASSERT_EQ_STR("b", grubenv_get(&env, "system_slot"));

    /* The next boot (GRUB passes system_slot=b) finds the overlay bound to B. */
    TEST_ASSERT_EQ_INT(0, boot_big(MACDISK_SLOT_B));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, macdisk_overlay_status());
    TEST_ASSERT_EQ_INT(0, block_read(macdisk_system(), 9, 1, back));
    TEST_ASSERT(memcmp(back, data, 512) == 0);

    /* Back to A (the overlay is empty: no force needed). */
    TEST_ASSERT_EQ_INT(0, macdisk_switch(MACDISK_SLOT_A, 0));
    TEST_ASSERT_EQ_INT(0, block_read(macdisk_system(), 9, 1, back));
    TEST_ASSERT(memcmp(back, a_before, 512) == 0);
    TEST_ASSERT_EQ_INT(0, boot_big(MACDISK_SLOT_A));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, macdisk_overlay_status());
    TEST_ASSERT_EQ_INT(0, macdisk_env_read(&env));
    TEST_ASSERT_EQ_STR("a", grubenv_get(&env, "system_slot"));

    /* Without a Boot partition holding grubenv, the switch happens but is not saved. */
    TEST_ASSERT_EQ_INT(0, boot(MACDISK_SLOT_A));
    TEST_ASSERT_EQ_INT(MACDISK_NOT_SAVED, macdisk_switch(MACDISK_SLOT_B, 1));
    TEST_ASSERT_EQ_INT(MACDISK_SLOT_B, macdisk_slot());
    free(big);
    big = NULL;
}

int main(void)
{
    TEST_RUN(test_slot_name);
    TEST_RUN(test_layout);
    TEST_RUN(test_persistence_and_reset);
    TEST_RUN(test_mismatch_after_image_change);
    TEST_RUN(test_no_layout);
    TEST_RUN(test_env_and_confirm);
    TEST_RUN(test_switch_and_promote);

    return TEST_REPORT("macdisk");
}
