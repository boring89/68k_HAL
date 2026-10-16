/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>

#include "kernel/block/block.h"
#include "kernel/block/ramdisk.h"
#include "kernel/fs/volume.h"

/* Built by the Makefile: tools/make_hfs_image.sh, then tools/make_disk_image.py. */
#define HFS_PATH "build/images/hfs.img"
#define DISK_PATH "build/images/disk.img"

static uint8_t buffer[1024 * 1024];

static size_t load(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL)
    {
        fprintf(stderr, "  cannot open %s\n", path);
        test_failures++;
        return 0;
    }

    size_t got = fread(buffer, 1, sizeof(buffer), f);

    fclose(f);

    return got;
}

static void reset(void)
{
    block_reset();
    ramdisk_reset();
    volume_unmount();
}

static void check_readme(void)
{
    hfs_volume_t *volume = volume_get();
    hfs_entry_t entry;
    char text[32];

    TEST_ASSERT(volume != NULL);

    if (volume == NULL)
        return;

    TEST_ASSERT_EQ_INT(0, strcmp(volume->name, "TESTVOL"));
    TEST_ASSERT_EQ_INT(0, hfs_lookup(volume, "ReadMe", &entry));
    TEST_ASSERT_EQ_INT(15, hfs_read(volume, &entry, HFS_FORK_DATA, 0, text, 32));
    TEST_ASSERT(memcmp(text, "Hello from HFS\r", 15) == 0);
}

static void test_bare_volume(void)
{
    reset();
    TEST_ASSERT_EQ_INT(-1, volume_mount_any());   /* no devices */
    TEST_ASSERT(volume_get() == NULL);

    size_t size = load(HFS_PATH);

    TEST_ASSERT_EQ_INT(0, ramdisk_register("hfs", buffer, size));
    TEST_ASSERT_EQ_INT(0, volume_mount_any());
    check_readme();

    volume_unmount();
    TEST_ASSERT(volume_get() == NULL);
}

static void test_partitioned_disk(void)
{
    reset();

    size_t size = load(DISK_PATH);

    TEST_ASSERT_EQ_INT(0, ramdisk_register("disk", buffer, size));
    TEST_ASSERT_EQ_INT(0, volume_mount_any());
    check_readme();

    /* The volume really is at block 64, not at the start of the disk. */
    TEST_ASSERT_EQ_INT(64, volume_get()->base);
}

static void test_skips_devices_without_hfs(void)
{
    static uint8_t zeros[4096];
    static uint8_t other[1024 * 1024];

    reset();
    size_t size = load(DISK_PATH);

    memcpy(other, buffer, size);
    TEST_ASSERT_EQ_INT(0, ramdisk_register("zero", zeros, sizeof(zeros)));
    TEST_ASSERT_EQ_INT(0, ramdisk_register("disk", other, size));
    TEST_ASSERT_EQ_INT(0, volume_mount_any());
    check_readme();

    reset();
    TEST_ASSERT_EQ_INT(0, ramdisk_register("zero", zeros, sizeof(zeros)));
    TEST_ASSERT_EQ_INT(-1, volume_mount_any());
}

int main(void)
{
    TEST_RUN(test_bare_volume);
    TEST_RUN(test_partitioned_disk);
    TEST_RUN(test_skips_devices_without_hfs);

    return TEST_REPORT("volume");
}
