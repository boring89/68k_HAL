/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/block/block.h"
#include "kernel/fs/hfs/hfs.h"
#include "kernel/mac/rsrc.h"
#include "kernel/mac/sysvm.h"

/* Built by tools/make_macsys_image.sh: a blessed System Folder with a 'zsys' file whose 'vm  ' 0 says "on". */
#define MACSYS_PATH "build/images/macsys.img"
/* Built by tools/make_hfs_image.sh: no blessed folder. */
#define HFS_PATH "build/images/hfs.img"
#define IMAGE_SIZE (800 * 1024)

static uint8_t image[IMAGE_SIZE];
static uint8_t original[IMAGE_SIZE];
static int writes;

static int mem_read(block_device_t *d, uint64_t lba, uint32_t count, void *buf)
{
    memcpy(buf, (uint8_t *)d->driver_data + lba * 512, (size_t)count * 512);
    return 0;
}

static int mem_write(block_device_t *d, uint64_t lba, uint32_t count, const void *buf)
{
    writes++;
    memcpy((uint8_t *)d->driver_data + lba * 512, buf, (size_t)count * 512);
    return 0;
}

static const block_ops_t rw_ops = {mem_read, mem_write};
static const block_ops_t ro_ops = {mem_read, NULL};
static block_device_t disk = {"sys", 512, IMAGE_SIZE / 512, &rw_ops, image};

static bool load(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL)
    {
        fprintf(stderr, "  cannot open %s\n", path);
        test_failures++;
        return false;
    }

    size_t got = fread(image, 1, sizeof(image), f);

    fclose(f);
    TEST_ASSERT_EQ_INT(IMAGE_SIZE, got);
    memcpy(original, image, sizeof(image));
    disk.ops = &rw_ops;
    writes = 0;

    return got == IMAGE_SIZE;
}

/* A resource fork in memory, read through the callback. */
typedef struct
{
    const uint8_t *bytes;
    uint32_t size;
    int fail_at;    /* fail the read that covers this offset (-1 = never) */
} fork_buf_t;

static int read_buf(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    fork_buf_t *fork = context;

    if (fork->fail_at >= 0 && (uint32_t)fork->fail_at >= offset &&
        (uint32_t)fork->fail_at < offset + length)
        return -1;

    memcpy(buffer, fork->bytes + offset, length);
    return 0;
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v >> 16);
    put16(p + 2, v);
}

/*
 * A fork with two types: 'ABCD' ids 5 and -3, 'WXYZ' id 7.
 * Data at 16: [len 3]"abc" [len 1]"x" [len 2]"hi"; map at 16 + 17.
 */
static uint32_t build_fork(uint8_t *f)
{
    uint32_t data = 16, map;
    uint8_t *p;

    memset(f, 0, 256);
    p = f + data;
    put32(p, 3);
    memcpy(p + 4, "abc", 3);
    put32(p + 7, 1);
    p[11] = 'x';
    put32(p + 12, 2);
    memcpy(p + 16, "hi", 2);
    map = data + 18;

    p = f + map;
    put16(p + 24, 28);              /* type list */
    put16(p + 26, 28 + 2 + 16 + 36); /* names (none used) */
    p += 28;
    put16(p, 1);                    /* two types */
    put32(p + 2, MAC_RSRC_TYPE('A', 'B', 'C', 'D'));
    put16(p + 6, 1);                /* two resources */
    put16(p + 8, 2 + 16);
    put32(p + 10, MAC_RSRC_TYPE('W', 'X', 'Y', 'Z'));
    put16(p + 14, 0);
    put16(p + 16, 2 + 16 + 24);
    p += 2 + 16;
    put16(p, 5);
    put32(p + 4, 0);                /* "abc" */
    put16(p + 12, (uint16_t)-3);
    put32(p + 16, 7);               /* "x" */
    put16(p + 24, 7);
    put32(p + 28, 12);              /* "hi" */

    uint32_t size = map + 28 + 2 + 16 + 36;

    put32(f, data);
    put32(f + 4, map);
    put32(f + 8, 18);
    put32(f + 12, size - map);

    return size;
}

static void test_rsrc_find(void)
{
    uint8_t bytes[256];
    uint32_t size = build_fork(bytes);
    fork_buf_t fork = {bytes, size, -1};
    uint32_t offset = 0, length = 0;

    TEST_ASSERT_EQ_INT(0, mac_rsrc_find(read_buf, &fork, size, MAC_RSRC_TYPE('A', 'B', 'C', 'D'), 5,
                                        &offset, &length));
    TEST_ASSERT_EQ_INT(3, length);
    TEST_ASSERT(memcmp(bytes + offset, "abc", 3) == 0);
    TEST_ASSERT_EQ_INT(0, mac_rsrc_find(read_buf, &fork, size, MAC_RSRC_TYPE('A', 'B', 'C', 'D'), -3,
                                        &offset, &length));
    TEST_ASSERT_EQ_INT(1, length);
    TEST_ASSERT_EQ_INT('x', bytes[offset]);
    TEST_ASSERT_EQ_INT(0, mac_rsrc_find(read_buf, &fork, size, MAC_RSRC_TYPE('W', 'X', 'Y', 'Z'), 7,
                                        &offset, &length));
    TEST_ASSERT_EQ_INT(2, length);
    TEST_ASSERT(memcmp(bytes + offset, "hi", 2) == 0);

    /* Not there: another id, another type, an empty fork. */
    TEST_ASSERT_EQ_INT(1, mac_rsrc_find(read_buf, &fork, size, MAC_RSRC_TYPE('A', 'B', 'C', 'D'), 6,
                                        &offset, &length));
    TEST_ASSERT_EQ_INT(1, mac_rsrc_find(read_buf, &fork, size, MAC_RSRC_TYPE('v', 'm', ' ', ' '), 0,
                                        &offset, &length));
    TEST_ASSERT_EQ_INT(1, mac_rsrc_find(read_buf, &fork, 0, MAC_RSRC_TYPE('A', 'B', 'C', 'D'), 5,
                                        &offset, &length));

    /* A read error, a fork cut short, a resource longer than the data area. */
    fork.fail_at = 72;   /* the second type entry */
    TEST_ASSERT_EQ_INT(-1, mac_rsrc_find(read_buf, &fork, size, MAC_RSRC_TYPE('W', 'X', 'Y', 'Z'), 7,
                                         &offset, &length));
    fork.fail_at = -1;
    TEST_ASSERT_EQ_INT(-1, mac_rsrc_find(read_buf, &fork, size - 10, MAC_RSRC_TYPE('A', 'B', 'C', 'D'), 5,
                                         &offset, &length));
    put32(bytes + 16 + 12, 3);
    TEST_ASSERT_EQ_INT(-1, mac_rsrc_find(read_buf, &fork, size, MAC_RSRC_TYPE('W', 'X', 'Y', 'Z'), 7,
                                         &offset, &length));
    TEST_ASSERT_EQ_INT(-1, mac_rsrc_find(NULL, &fork, size, MAC_RSRC_TYPE('W', 'X', 'Y', 'Z'), 7,
                                         &offset, &length));
}

/* The 'vm  ' 0 resource as hfs_read sees it. */
static bool vm_resource(uint8_t flag[4])
{
    hfs_volume_t volume;
    hfs_entry_t entry;
    uint32_t offset, length;

    if (hfs_mount(&volume, &disk, 0, 0) != 0 ||
        hfs_lookup(&volume, "System Folder:System", &entry) != 0)
        return false;

    uint8_t fork[8192];

    if (entry.rsrc_length > sizeof(fork) ||
        hfs_read(&volume, &entry, HFS_FORK_RESOURCE, 0, fork, entry.rsrc_length) != (int)entry.rsrc_length)
        return false;

    fork_buf_t buf = {fork, entry.rsrc_length, -1};

    if (mac_rsrc_find(read_buf, &buf, entry.rsrc_length, MAC_RSRC_TYPE('v', 'm', ' ', ' '), 0, &offset,
                      &length) != 0 ||
        length < 4)
        return false;

    memcpy(flag, fork + offset, 4);
    /* The size of the backing file follows: 10 MB. */
    TEST_ASSERT_EQ_INT(10 * 1024 * 1024, (fork[offset + 4] << 24) | (fork[offset + 5] << 16) |
                                             (fork[offset + 6] << 8) | fork[offset + 7]);

    return true;
}

static void test_turn_off(void)
{
    uint8_t flag[4];

    if (!load(MACSYS_PATH))
        return;

    hfs_volume_t volume;

    TEST_ASSERT_EQ_INT(0, hfs_mount(&volume, &disk, 0, 0));
    TEST_ASSERT(volume.blessed_folder != 0);
    TEST_ASSERT(vm_resource(flag));
    TEST_ASSERT_EQ_INT(1, flag[3]);

    TEST_ASSERT_EQ_INT(1, sysvm_turn_off(&disk));
    TEST_ASSERT(vm_resource(flag));
    TEST_ASSERT(memcmp(flag, "\0\0\0\0", 4) == 0);

    /* Exactly one byte of the volume changed (the flag word was 0, 0, 0, 1). */
    int changed = 0;

    for (size_t i = 0; i < sizeof(image); i++)
        changed += image[i] != original[i];
    TEST_ASSERT_EQ_INT(1, changed);
    TEST_ASSERT_EQ_INT(1, writes);

    /* Already off: nothing to do, nothing written. */
    TEST_ASSERT_EQ_INT(0, sysvm_turn_off(&disk));
    TEST_ASSERT_EQ_INT(1, writes);
}

static void test_read_only_and_absent(void)
{
    if (!load(MACSYS_PATH))
        return;

    /* On, but the disk cannot be written. */
    disk.ops = &ro_ops;
    TEST_ASSERT_EQ_INT(-1, sysvm_turn_off(&disk));
    TEST_ASSERT(memcmp(image, original, sizeof(image)) == 0);

    /* No blessed folder: not a system disk. */
    if (!load(HFS_PATH))
        return;
    TEST_ASSERT_EQ_INT(0, sysvm_turn_off(&disk));
    TEST_ASSERT_EQ_INT(0, writes);

    /* Not HFS at all. */
    memset(image, 0, sizeof(image));
    TEST_ASSERT_EQ_INT(0, sysvm_turn_off(&disk));
    TEST_ASSERT_EQ_INT(0, sysvm_turn_off(NULL));
}

int main(void)
{
    TEST_RUN(test_rsrc_find);
    TEST_RUN(test_turn_off);
    TEST_RUN(test_read_only_and_absent);

    return TEST_REPORT("sysvm");
}
