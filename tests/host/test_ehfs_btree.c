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

#define SECTORS (65536u * 8u)       /* 256 MiB */

static uint8_t *disk;
static ehfs_t fs;
static ehfs_check_t report;

static int mem_read(block_device_t *d, uint64_t lba, uint32_t count, void *buf)
{
    (void)d;
    memcpy(buf, disk + lba * 512, (size_t)count * 512);
    return 0;
}

static int mem_write(block_device_t *d, uint64_t lba, uint32_t count, const void *buf)
{
    (void)d;
    memcpy(disk + lba * 512, buf, (size_t)count * 512);
    return 0;
}

static const block_ops_t ops = {mem_read, mem_write};
static block_device_t dev = {"ehfs", 512, SECTORS, &ops, NULL};

/*
 * These records are not catalog or extent records: the trees are checked with ehfs_bt_check only (the
 * whole-volume fsck also checks what the records mean).
 */

/* The reference: which keys are in the tree, and the length of each value. */
#define KEYS 4000
static int16_t present[KEYS];      /* -1 = absent, else the value's length */
static uint8_t seed_of[KEYS];

static uint32_t make_key(int k, uint8_t *key)
{
    /* Keys of different lengths that sort in an order unrelated to k. */
    uint32_t h = (uint32_t)k * 2654435761u;
    uint32_t n = 4 + (h >> 28) % 40;

    for (uint32_t i = 0; i < n; i++)
        key[i] = (uint8_t)("abcdefghijklmnopqrstuvwxyz0123456789"[(h >> (i % 24)) % 36] + (i == n - 1 ? 0 : 0));
    key[0] = (uint8_t)(h >> 24);
    key[1] = (uint8_t)(k >> 8);
    key[2] = (uint8_t)k;
    return n;
}

static void make_value(int k, uint8_t seed, uint32_t length, uint8_t *value)
{
    for (uint32_t i = 0; i < length; i++)
        value[i] = (uint8_t)(k * 31 + i * 7 + seed);
}

static void setup(void)
{
    ehfs_format_options_t o;

    memset(&o, 0, sizeof o);
    o.name = "Tree";
    memset(disk, 0, (size_t)SECTORS * 512);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_format(&fs, &dev, 0, SECTORS, &o));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, 0, false));
    for (int i = 0; i < KEYS; i++)
        present[i] = -1;
}

typedef struct
{
    uint8_t last[EHFS_BT_KEY_MAX];
    uint32_t last_length;
    int count;
    bool sorted;
} walk_t;

static int walker(void *ctx, const uint8_t *key, uint32_t key_length, const uint8_t *value, uint32_t value_length)
{
    walk_t *w = ctx;
    uint32_t n = key_length < w->last_length ? key_length : w->last_length;
    int c = memcmp(w->last, key, n);

    (void)value;
    (void)value_length;
    if (w->count > 0 && (c > 0 || (c == 0 && w->last_length >= key_length)))
        w->sorted = false;
    memcpy(w->last, key, key_length);
    w->last_length = key_length;
    w->count++;
    return 0;
}

static void verify(int tree)
{
    uint8_t key[64], value[4096], want[4096];
    uint32_t length;
    int expected = 0;

    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_bt_check(&fs, tree, &report));
    if (report.errors)
        printf("    btree check: %s\n", report.first);
    TEST_ASSERT_EQ_INT(0, report.errors);
    for (int k = 0; k < KEYS; k++)
    {
        uint32_t n = make_key(k, key);
        int r = ehfs_bt_find(&fs, tree, key, n, value, sizeof value, &length);

        if (present[k] < 0)
        {
            TEST_ASSERT_EQ_INT(EHFS_ENOENT, r);
            continue;
        }
        expected++;
        TEST_ASSERT_EQ_INT(EHFS_OK, r);
        TEST_ASSERT_EQ_INT(present[k], length);
        make_value(k, seed_of[k], length, want);
        TEST_ASSERT(memcmp(value, want, length) == 0);
    }

    walk_t w = {{0}, 0, 0, true};

    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_bt_walk(&fs, tree, (const uint8_t *)"", 0, walker, &w));
    TEST_ASSERT_EQ_INT(expected, w.count);
    TEST_ASSERT(w.sorted);
    TEST_ASSERT_EQ_INT(expected, ehfs_bt_records(&fs, tree));
}

static void test_random(void)
{
    uint8_t key[64], value[4096];

    setup();
    srand(7);
    for (int op = 0; op < 12000; op++)
    {
        int k = rand() % KEYS;
        uint32_t n = make_key(k, key);
        int what = rand() % 10;

        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
        if (what < 6)
        {
            /* Insert: values from a few bytes to 3 KiB, so that nodes split often. */
            uint32_t length = rand() % 4 == 0 ? 1000 + (uint32_t)(rand() % 2000) : (uint32_t)(rand() % 200);
            uint8_t seed = (uint8_t)rand();

            make_value(k, seed, length, value);
            int r = ehfs_bt_insert(&fs, EHFS_FORK_EXTENTS, key, n, value, length);

            TEST_ASSERT_EQ_INT(present[k] >= 0 ? EHFS_EEXIST : EHFS_OK, r);
            if (r == EHFS_OK)
            {
                present[k] = (int16_t)length;
                seed_of[k] = seed;
            }
        }
        else if (what < 9)
        {
            int r = ehfs_bt_delete(&fs, EHFS_FORK_EXTENTS, key, n);

            TEST_ASSERT_EQ_INT(present[k] >= 0 ? EHFS_OK : EHFS_ENOENT, r);
            present[k] = -1;
        }
        else
        {
            uint32_t length = (uint32_t)(rand() % 300);
            uint8_t seed = (uint8_t)rand();

            make_value(k, seed, length, value);
            int r = ehfs_bt_replace(&fs, EHFS_FORK_EXTENTS, key, n, value, length);

            TEST_ASSERT_EQ_INT(present[k] >= 0 ? EHFS_OK : EHFS_ENOENT, r);
            if (r == EHFS_OK)
            {
                present[k] = (int16_t)length;
                seed_of[k] = seed;
            }
        }
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_commit(&fs));
        if (op % 1500 == 1499)
            verify(EHFS_FORK_EXTENTS);
    }
    verify(EHFS_FORK_EXTENTS);

    /* The tree outgrew its first 8 nodes: its file grew (the catalog test checks the blocks with fsck). */
    TEST_ASSERT(ehfs_le64(ehfs_sb_fork(&fs, EHFS_FORK_EXTENTS) + FORK_BLOCKS) > 8 * EHFS_NODE_BLOCKS);

    /* Everything deleted: an empty tree. */
    for (int k = 0; k < KEYS; k++)
    {
        if (present[k] < 0)
            continue;
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_bt_delete(&fs, EHFS_FORK_EXTENTS, key, make_key(k, key)));
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_commit(&fs));
        present[k] = -1;
    }
    verify(EHFS_FORK_EXTENTS);
    TEST_ASSERT_EQ_INT(0, ehfs_bt_records(&fs, EHFS_FORK_EXTENTS));

    /* Remounted, the same. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_unmount(&fs));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_mount(&fs, &dev, 0, true));
    verify(EHFS_FORK_EXTENTS);
}

static void test_sequential_and_bounds(void)
{
    uint8_t key[8], value[64];

    setup();
    /* Ascending keys (always the last leaf), then descending (always the first). */
    for (int i = 0; i < 3000; i++)
    {
        key[0] = (uint8_t)(i >> 8);
        key[1] = (uint8_t)i;
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_bt_insert(&fs, EHFS_FORK_ATTRIBUTES, key, 2, value, 60));
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_commit(&fs));
    }
    for (int i = 2999; i >= 0; i -= 2)
    {
        key[0] = (uint8_t)(i >> 8);
        key[1] = (uint8_t)i;
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_bt_delete(&fs, EHFS_FORK_ATTRIBUTES, key, 2));
        TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_commit(&fs));
    }
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_bt_check(&fs, EHFS_FORK_ATTRIBUTES, &report));
    if (report.errors)
        printf("    btree check: %s\n", report.first);
    TEST_ASSERT_EQ_INT(0, report.errors);
    TEST_ASSERT_EQ_INT(1500, ehfs_bt_records(&fs, EHFS_FORK_ATTRIBUTES));

    /* Keys smaller than everything after the first leaf went away (the left edge). */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_bt_insert(&fs, EHFS_FORK_ATTRIBUTES, (const uint8_t *)"", 0, value, 1));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_commit(&fs));
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_bt_check(&fs, EHFS_FORK_ATTRIBUTES, &report));
    TEST_ASSERT_EQ_INT(0, report.errors);

    /* Too large. */
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_begin(&fs));
    TEST_ASSERT_EQ_INT(EHFS_EINVAL, ehfs_bt_insert(&fs, EHFS_FORK_ATTRIBUTES, key, 2, disk, 9000));
    ehfs_abort(&fs);
    TEST_ASSERT_EQ_INT(EHFS_OK, ehfs_bt_check(&fs, EHFS_FORK_ATTRIBUTES, &report));
    TEST_ASSERT_EQ_INT(0, report.errors);
}

int main(void)
{
    disk = malloc((size_t)SECTORS * 512);
    if (disk == NULL)
        return 1;
    TEST_RUN(test_random);
    TEST_RUN(test_sequential_and_bounds);
    free(disk);

    return TEST_REPORT("ehfs_btree");
}
