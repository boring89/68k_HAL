/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/block/block.h"
#include "kernel/fs/ehfs/btree.h"
#include "kernel/fs/ehfs/ehfs.h"
#include "kernel/fs/ehfs/file.h"

#define SECTORS (16384u * 8u)       /* 64 MiB */

static uint8_t *disk;
static ehfs_t fs;
static ehfs_check_t report;
static uint8_t *buf, *want;

static int mem_read(block_device_t *d, uint64_t lba, uint32_t count, void *b)
{
    (void)d;
    memcpy(b, disk + lba * 512, (size_t)count * 512);
    return 0;
}

static int mem_write(block_device_t *d, uint64_t lba, uint32_t count, const void *b)
{
    (void)d;
    memcpy(disk + lba * 512, b, (size_t)count * 512);
    return 0;
}

static const block_ops_t ops = {mem_read, mem_write};
static block_device_t dev = {"ehfs", 512, SECTORS, &ops, NULL};

#define S(x) (const uint8_t *)(x), (uint32_t)strlen(x)

static void fresh(void)
{
    ehfs_format_options_t o;

    memset(&o, 0, sizeof o);
    o.name = "Data";
    memset(disk, 0x5A, (size_t)SECTORS * 512);     /* "old data" everywhere */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_format(&fs, &dev, 0, SECTORS, &o));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, 0, false));
}

static void fsck(void)
{
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_check(&fs, &report));
    if (report.errors)
        printf("    fsck: %s\n", report.first);
    TEST_ASSERT_EQ_INT(0, report.errors);
}

static void pattern(uint8_t *p, uint32_t n, uint32_t seed)
{
    for (uint32_t i = 0; i < n; i++)
        p[i] = (uint8_t)((i * 131 + i / 4096 + seed) ^ 0x3C);
}

static uint64_t length_of(uint64_t cnid, int which)
{
    ehfs_entry_t e;

    if (ehfs_cat_get(&fs, cnid, &e) != EHFS_OK)
        return UINT64_MAX;
    return ehfs_le64((which ? e.rsrc_fork : e.data_fork) + FORK_LENGTH);
}

static bool read_is(uint64_t cnid, int which, uint64_t offset, const uint8_t *expect, uint32_t n)
{
    uint32_t done;

    if (ehfs_read(&fs, cnid, which, offset, buf, n, &done) != EHFS_OK || done != n)
        return false;
    return memcmp(buf, expect, n) == 0;
}

static void test_basic(void)
{
    ehfs_entry_t f;
    uint32_t done;

    fresh();
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_create(&fs, EHFS_CNID_ROOT, S("notes.txt"), false, 10, &f));

    /* Small writes inside one block, then across blocks. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_write(&fs, f.cnid, EHFS_DATA_FORK, 0, "Hello", 5, 20));
    TEST_ASSERT_EQ_INT(5, length_of(f.cnid, EHFS_DATA_FORK));
    TEST_ASSERT(read_is(f.cnid, EHFS_DATA_FORK, 0, (const uint8_t *)"Hello", 5));
    pattern(want, 10000, 1);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_write(&fs, f.cnid, EHFS_DATA_FORK, 3, want, 10000, 30));
    TEST_ASSERT_EQ_INT(10003, length_of(f.cnid, EHFS_DATA_FORK));
    TEST_ASSERT(read_is(f.cnid, EHFS_DATA_FORK, 0, (const uint8_t *)"Hel", 3));
    TEST_ASSERT(read_is(f.cnid, EHFS_DATA_FORK, 3, want, 10000));

    /* Reading past the end: short, then nothing. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_read(&fs, f.cnid, EHFS_DATA_FORK, 10000, buf, 100, &done));
    TEST_ASSERT_EQ_INT(3, done);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_read(&fs, f.cnid, EHFS_DATA_FORK, 20000, buf, 100, &done));
    TEST_ASSERT_EQ_INT(0, done);

    /* A write past the end: the gap reads as zeros (not the 0x5A on the disk). */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_write(&fs, f.cnid, EHFS_DATA_FORK, 30000, "end", 3, 40));
    TEST_ASSERT_EQ_INT(30003, length_of(f.cnid, EHFS_DATA_FORK));
    memset(want, 0, 30000 - 10003);
    TEST_ASSERT(read_is(f.cnid, EHFS_DATA_FORK, 10003, want, 30000 - 10003));
    TEST_ASSERT(read_is(f.cnid, EHFS_DATA_FORK, 30000, (const uint8_t *)"end", 3));

    /* The resource fork is separate. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_write(&fs, f.cnid, EHFS_RESOURCE_FORK, 0, "rsrc", 4, 50));
    TEST_ASSERT_EQ_INT(4, length_of(f.cnid, EHFS_RESOURCE_FORK));
    TEST_ASSERT(read_is(f.cnid, EHFS_RESOURCE_FORK, 0, (const uint8_t *)"rsrc", 4));
    TEST_ASSERT_EQ_INT(30003, length_of(f.cnid, EHFS_DATA_FORK));

    /* Times and Finder info. */
    ehfs_entry_t e;

    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_get(&fs, f.cnid, &e));
    TEST_ASSERT_EQ_INT(50, e.times[1]);
    memcpy(e.finder, "TEXTttxt", 8);
    e.flags = 1;
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_set_info(&fs, &e));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_cat_get(&fs, f.cnid, &e));
    TEST_ASSERT(memcmp(e.finder, "TEXTttxt", 8) == 0 && e.flags == 1);
    TEST_ASSERT_EQ_INT(30003, ehfs_le64(e.data_fork + FORK_LENGTH));

    /* Not a file. */
    TEST_ASSERT_EQ_INT(EHFS_EISDIR, ehfs_write(&fs, EHFS_CNID_ROOT, EHFS_DATA_FORK, 0, "x", 1, 1));
    fsck();
}

/* Two files written in turns get many runs each: more than 8, so the extents tree is used. */
static void test_fragments(void)
{
    ehfs_entry_t a, b;
    uint64_t free0;

    fresh();
    free0 = ehfs_sb_get64(&fs, SB_FREE_BLOCKS);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_create(&fs, EHFS_CNID_ROOT, S("a"), false, 1, &a));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_create(&fs, EHFS_CNID_ROOT, S("b"), false, 1, &b));
    pattern(want, 3 * 1024 * 1024, 7);
    for (uint32_t off = 0; off < 3 * 1024 * 1024; off += 3 * 4096)
    {
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_write(&fs, a.cnid, EHFS_DATA_FORK, off, want + off, 3 * 4096, 2));
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_write(&fs, b.cnid, EHFS_DATA_FORK, off, want + off, 2 * 4096, 2));
    }
    TEST_ASSERT(ehfs_bt_records(&fs, EHFS_FORK_EXTENTS) > 20);
    TEST_ASSERT(read_is(a.cnid, EHFS_DATA_FORK, 0, want, 3 * 1024 * 1024));
    TEST_ASSERT(read_is(a.cnid, EHFS_DATA_FORK, 12345, want + 12345, 99999));
    fsck();

    /* Shorter: blocks and extents records go; longer again: zeros, not the old bytes. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_set_length(&fs, a.cnid, EHFS_DATA_FORK, 1000, 3));
    TEST_ASSERT_EQ_INT(1000, length_of(a.cnid, EHFS_DATA_FORK));
    TEST_ASSERT(read_is(a.cnid, EHFS_DATA_FORK, 0, want, 1000));
    fsck();
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_set_length(&fs, a.cnid, EHFS_DATA_FORK, 50000, 4));
    TEST_ASSERT_EQ_INT(50000, length_of(a.cnid, EHFS_DATA_FORK));
    memset(buf + 100000, 0, 49000);
    TEST_ASSERT(read_is(a.cnid, EHFS_DATA_FORK, 1000, buf + 100000, 49000));
    fsck();

    /* Removed: every block back, no extents records left. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_remove(&fs, EHFS_CNID_ROOT, S("a"), 5));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_remove(&fs, EHFS_CNID_ROOT, S("b"), 5));
    TEST_ASSERT_EQ_INT(0, ehfs_bt_records(&fs, EHFS_FORK_EXTENTS));
    fsck();
    TEST_ASSERT(ehfs_sb_get64(&fs, SB_FREE_BLOCKS) >= free0 - 64);   /* the trees may have grown */
    TEST_ASSERT_EQ_INT(0, ehfs_sb_get64(&fs, SB_FILE_COUNT));
}

static void test_attributes(void)
{
    ehfs_entry_t f;
    uint8_t name[300];
    uint32_t n, len;

    fresh();
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_create(&fs, EHFS_CNID_ROOT, S("doc"), false, 1, &f));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_attr_set(&fs, f.cnid, S("com.apple.FinderInfo"), "0123456789abcdef", 16));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_attr_set(&fs, f.cnid, S("user.comment"), "hello", 5));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_attr_set(&fs, f.cnid, S("user.comment"), "a longer comment", 16));
    pattern(want, 3072, 3);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_attr_set(&fs, EHFS_CNID_ROOT, S("user.big"), want, 3072));
    TEST_ASSERT_EQ_INT(EHFS_EINVAL, ehfs_attr_set(&fs, f.cnid, S("user.toobig"), want, 3073));
    TEST_ASSERT_EQ_INT(EHFS_ENOENT, ehfs_attr_set(&fs, 9999, S("user.x"), "x", 1));

    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_attr_get(&fs, f.cnid, S("user.comment"), buf, 100, &len));
    TEST_ASSERT(len == 16 && memcmp(buf, "a longer comment", 16) == 0);
    TEST_ASSERT_EQ_INT(EHFS_ENOENT, ehfs_attr_get(&fs, f.cnid, S("USER.COMMENT"), buf, 100, &len));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_attr_get(&fs, EHFS_CNID_ROOT, S("user.big"), buf, 4000, &len));
    TEST_ASSERT(len == 3072 && memcmp(buf, want, 3072) == 0);

    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_attr_name(&fs, f.cnid, 0, name, &n));
    TEST_ASSERT(n == 20 && memcmp(name, "com.apple.FinderInfo", 20) == 0);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_attr_name(&fs, f.cnid, 1, name, &n));
    TEST_ASSERT(n == 12 && memcmp(name, "user.comment", 12) == 0);
    TEST_ASSERT_EQ_INT(EHFS_ENOENT, ehfs_attr_name(&fs, f.cnid, 2, name, &n));
    fsck();

    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_attr_remove(&fs, f.cnid, S("user.comment")));
    TEST_ASSERT_EQ_INT(EHFS_ENOENT, ehfs_attr_remove(&fs, f.cnid, S("user.comment")));

    /* Removing the file removes its attributes; the root's stays. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_remove(&fs, EHFS_CNID_ROOT, S("doc"), 2));
    TEST_ASSERT_EQ_INT(1, ehfs_bt_records(&fs, EHFS_FORK_ATTRIBUTES));
    fsck();
}

/* A crash anywhere in a write that makes a file longer: the old part is intact, the length old or new. */
static void test_crash(void)
{
    int seen[2] = {0, 0};

    pattern(want, 200000, 9);
    for (int k = 0; k < 60; k += 2)
    {
        ehfs_entry_t f;

        fresh();
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_create(&fs, EHFS_CNID_ROOT, S("f"), false, 1, &f));
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_write(&fs, f.cnid, EHFS_DATA_FORK, 0, want, 50000, 1));
        fs.crash_after = k;
        ehfs_write(&fs, f.cnid, EHFS_DATA_FORK, 50000, want + 50000, 150000, 2);
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, 0, false));
        fsck();

        uint64_t length = length_of(f.cnid, EHFS_DATA_FORK);

        TEST_ASSERT(length == 50000 || length == 200000);
        TEST_ASSERT(read_is(f.cnid, EHFS_DATA_FORK, 0, want, (uint32_t)length));
        seen[length == 200000]++;
    }
    TEST_ASSERT(seen[0] > 0 && seen[1] > 0);
}

int main(void)
{
    disk = malloc((size_t)SECTORS * 512);
    buf = malloc(4 * 1024 * 1024);
    want = malloc(4 * 1024 * 1024);
    if (disk == NULL || buf == NULL || want == NULL)
        return 1;
    TEST_RUN(test_basic);
    TEST_RUN(test_fragments);
    TEST_RUN(test_attributes);
    TEST_RUN(test_crash);
    free(disk);
    free(buf);
    free(want);

    return TEST_REPORT("ehfs_file");
}
