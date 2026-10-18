/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>
#include <stdlib.h>

#include "kernel/block/block.h"
#include "kernel/block/overlay.h"
#include "kernel/memory/heap.h"

#define BASE_BLOCKS 64
#define STORE_BLOCKS 20     /* 1 header + 1 map + 18 data */

static uint8_t base_mem[BASE_BLOCKS * 512];
static uint8_t store_mem[STORE_BLOCKS * 512];
static uint8_t arena[256 * 1024] __attribute__((aligned(16)));

/* Writes allowed before the "power fails" (-1 = no limit); counts writes. */
static int write_budget = -1;
static int base_writes;
static int store_writes;

static int mem_read(block_device_t *d, uint64_t lba, uint32_t count, void *buf)
{
    memcpy(buf, (uint8_t *)d->driver_data + lba * 512, (size_t)count * 512);
    return 0;
}

static int base_write(block_device_t *d, uint64_t lba, uint32_t count,
                      const void *buf)
{
    base_writes++;
    memcpy((uint8_t *)d->driver_data + lba * 512, buf, (size_t)count * 512);
    return 0;
}

/* The power fails after write_budget single-block writes. */
static int store_write(block_device_t *d, uint64_t lba, uint32_t count,
                       const void *buf)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (write_budget == 0)
            return -1;

        if (write_budget > 0)
            write_budget--;

        store_writes++;
        memcpy((uint8_t *)d->driver_data + (lba + i) * 512,
               (const uint8_t *)buf + (size_t)i * 512, 512);
    }

    return 0;
}

static const block_ops_t base_ops = {mem_read, base_write};
static const block_ops_t store_ops = {mem_read, store_write};

static block_device_t base_dev = {"base", 512, BASE_BLOCKS, &base_ops, base_mem};
static block_device_t store_dev = {"store", 512, STORE_BLOCKS, &store_ops, store_mem};
static overlay_t ov;

static void fill_base(void)
{
    for (size_t i = 0; i < sizeof(base_mem); i++)
        base_mem[i] = (uint8_t)(i * 7 + i / 512);
}

static void setup(void)
{
    block_reset();
    heap_init((uintptr_t)arena, sizeof(arena));
    fill_base();
    memset(store_mem, 0, sizeof(store_mem));
    write_budget = -1;
    base_writes = store_writes = 0;
}

static void pattern(uint8_t *buf, int blocks, uint8_t seed)
{
    for (int i = 0; i < blocks * 512; i++)
        buf[i] = (uint8_t)(seed + i);
}

static overlay_status_t reopen(uint32_t slot)
{
    block_reset();
    return overlay_open(&ov, "ovl", &base_dev, &store_dev, slot);
}

static void test_empty_store(void)
{
    setup();
    TEST_ASSERT_EQ_INT(OVERLAY_EMPTY, reopen(0));
    TEST_ASSERT(block_device_find("ovl") == NULL);
}

static void test_read_write(void)
{
    uint8_t data[3 * 512];
    uint8_t back[3 * 512];
    uint8_t orig[64 * 512];

    setup();
    memcpy(orig, base_mem, sizeof(orig));
    TEST_ASSERT_EQ_INT(0, overlay_format(&store_dev, &base_dev, 0));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(0));

    block_device_t *d = block_device_find("ovl");

    TEST_ASSERT(d != NULL);
    TEST_ASSERT_EQ_INT(BASE_BLOCKS, d->block_count);

    /* Untouched: reads come from the base. */
    TEST_ASSERT_EQ_INT(0, block_read(d, 0, 3, back));
    TEST_ASSERT(memcmp(back, orig, sizeof(back)) == 0);
    pattern(data, 3, 0x11);
    TEST_ASSERT_EQ_INT(0, block_write(d, 10, 3, data));
    TEST_ASSERT_EQ_INT(0, block_read(d, 10, 3, back));
    TEST_ASSERT(memcmp(data, back, sizeof(data)) == 0);
    TEST_ASSERT_EQ_INT(3, overlay_used_blocks(&ov));

    /* A read across written and unwritten blocks mixes both sources. */
    uint8_t wide[8 * 512];

    TEST_ASSERT_EQ_INT(0, block_read(d, 8, 8, wide));
    TEST_ASSERT(memcmp(wide, orig + 8 * 512, 2 * 512) == 0);
    TEST_ASSERT(memcmp(wide + 2 * 512, data, 3 * 512) == 0);
    TEST_ASSERT(memcmp(wide + 5 * 512, orig + 13 * 512, 3 * 512) == 0);

    /* The base is never written. */
    TEST_ASSERT_EQ_INT(0, base_writes);
    TEST_ASSERT(memcmp(base_mem, orig, sizeof(orig)) == 0);

    /* Overwriting a mapped block takes no new data block. */
    pattern(data, 1, 0x55);
    TEST_ASSERT_EQ_INT(0, block_write(d, 11, 1, data));
    TEST_ASSERT_EQ_INT(3, overlay_used_blocks(&ov));
}

static void test_persists(void)
{
    uint8_t data[2 * 512];
    uint8_t back[2 * 512];

    setup();
    TEST_ASSERT_EQ_INT(0, overlay_format(&store_dev, &base_dev, 1));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(1));
    pattern(data, 2, 0x77);
    TEST_ASSERT_EQ_INT(0, block_write(block_device_find("ovl"), 5, 2, data));

    /* "Reboot": a fresh open sees the same content. */
    TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(1));
    TEST_ASSERT_EQ_INT(2, overlay_used_blocks(&ov));
    TEST_ASSERT_EQ_INT(0, block_read(block_device_find("ovl"), 5, 2, back));
    TEST_ASSERT(memcmp(data, back, sizeof(data)) == 0);

    /* A new write after reopening goes to a fresh data block. */
    pattern(data, 1, 0x99);
    TEST_ASSERT_EQ_INT(0, block_write(block_device_find("ovl"), 20, 1, data));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(1));
    TEST_ASSERT_EQ_INT(3, overlay_used_blocks(&ov));
    TEST_ASSERT_EQ_INT(0, block_read(block_device_find("ovl"), 20, 1, back));
    TEST_ASSERT(memcmp(data, back, 512) == 0);
}

static void test_mismatch(void)
{
    setup();
    TEST_ASSERT_EQ_INT(0, overlay_format(&store_dev, &base_dev, 0));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(0));

    /* Another slot's overlay is not applied. */
    TEST_ASSERT_EQ_INT(OVERLAY_MISMATCH, reopen(1));
    TEST_ASSERT(block_device_find("ovl") == NULL);

    /* So is one whose base changed (inside the last MiB here: all of it). */
    base_mem[40 * 512] ^= 1;
    TEST_ASSERT_EQ_INT(OVERLAY_MISMATCH, reopen(0));

    /* Reset binds the overlay to the base as it is now. */
    TEST_ASSERT_EQ_INT(0, overlay_format(&store_dev, &base_dev, 0));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(0));
    TEST_ASSERT_EQ_INT(0, overlay_used_blocks(&ov));
}

static void test_reset(void)
{
    uint8_t data[512];
    uint8_t back[512];

    setup();
    TEST_ASSERT_EQ_INT(0, overlay_format(&store_dev, &base_dev, 0));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(0));
    pattern(data, 1, 3);
    TEST_ASSERT_EQ_INT(0, block_write(block_device_find("ovl"), 2, 1, data));

    TEST_ASSERT_EQ_INT(0, overlay_format(&store_dev, &base_dev, 0));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(0));
    TEST_ASSERT_EQ_INT(0, overlay_used_blocks(&ov));
    TEST_ASSERT_EQ_INT(0, block_read(block_device_find("ovl"), 2, 1, back));
    TEST_ASSERT(memcmp(back, base_mem + 2 * 512, 512) == 0);

    /* Reset of an open overlay, without reopening it. */
    TEST_ASSERT_EQ_INT(0, block_write(block_device_find("ovl"), 2, 1, data));
    TEST_ASSERT_EQ_INT(1, overlay_used_blocks(&ov));
    TEST_ASSERT_EQ_INT(0, overlay_reset(&ov));
    TEST_ASSERT_EQ_INT(0, overlay_used_blocks(&ov));
    TEST_ASSERT_EQ_INT(0, block_read(block_device_find("ovl"), 2, 1, back));
    TEST_ASSERT(memcmp(back, base_mem + 2 * 512, 512) == 0);
    TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(0));
    TEST_ASSERT_EQ_INT(0, overlay_used_blocks(&ov));
}

static void test_full(void)
{
    uint8_t data[30 * 512];

    setup();
    TEST_ASSERT_EQ_INT(0, overlay_format(&store_dev, &base_dev, 0));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(0));
    TEST_ASSERT_EQ_INT(18, overlay_capacity_blocks(&ov));
    pattern(data, 30, 1);

    block_device_t *d = block_device_find("ovl");

    /* Too many new blocks for one write: refused, nothing changed. */
    TEST_ASSERT_EQ_INT(-1, block_write(d, 0, 19, data));
    TEST_ASSERT_EQ_INT(0, overlay_used_blocks(&ov));
    TEST_ASSERT_EQ_INT(0, block_write(d, 0, 18, data));
    TEST_ASSERT_EQ_INT(-1, block_write(d, 40, 1, data));
    /* Mapped blocks can still be overwritten. */
    TEST_ASSERT_EQ_INT(0, block_write(d, 3, 1, data));
}

/* Cut the power after every possible number of store writes. */
static void test_power_fail(void)
{
    uint8_t first[2 * 512];
    uint8_t second[4 * 512];
    uint8_t back[4 * 512];

    pattern(first, 2, 0x21);
    pattern(second, 4, 0x42);

    for (int budget = 0; budget < 20; budget++)
    {
        setup();
        TEST_ASSERT_EQ_INT(0, overlay_format(&store_dev, &base_dev, 0));
        TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(0));
        TEST_ASSERT_EQ_INT(0, block_write(block_device_find("ovl"), 8, 2, first));

        write_budget = budget;
        int rc = block_write(block_device_find("ovl"), 9, 4, second);

        /* Reboot after the failure. */
        write_budget = -1;
        TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(0));

        block_device_t *d = block_device_find("ovl");

        /* Block 8 was committed before: it must be intact. */
        TEST_ASSERT_EQ_INT(0, block_read(d, 8, 1, back));
        TEST_ASSERT(memcmp(back, first, 512) == 0);

        /* Every block is either as before or as in the new write. */
        for (int b = 0; b < 4; b++)
        {
            TEST_ASSERT_EQ_INT(0, block_read(d, 9 + b, 1, back));

            int old = b == 0 ? memcmp(back, first + 512, 512) == 0
                             : memcmp(back, base_mem + (9 + b) * 512, 512) == 0;
            int upd = memcmp(back, second + b * 512, 512) == 0;

            TEST_ASSERT(old || upd);

            /* A successful write is complete. */
            if (rc == 0)
                TEST_ASSERT(upd);
        }

        /* And the overlay keeps working. */
        TEST_ASSERT_EQ_INT(0, block_write(d, 30, 1, first));
        TEST_ASSERT_EQ_INT(0, block_read(d, 30, 1, back));
        TEST_ASSERT(memcmp(back, first, 512) == 0);
    }
}

/* A reset interrupted at every point is finished by the next open. */
static void test_reset_interrupted(void)
{
    uint8_t data[512];

    pattern(data, 1, 9);

    for (int budget = 0; budget < 3; budget++)
    {
        setup();
        TEST_ASSERT_EQ_INT(0, overlay_format(&store_dev, &base_dev, 0));
        TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(0));
        TEST_ASSERT_EQ_INT(0, block_write(block_device_find("ovl"), 4, 1, data));

        write_budget = budget;
        TEST_ASSERT_EQ_INT(-1, overlay_format(&store_dev, &base_dev, 0));
        write_budget = -1;

        /* Either the old overlay is intact, or the reset completes. */
        overlay_status_t st = reopen(0);

        TEST_ASSERT_EQ_INT(OVERLAY_OK, st);

        uint32_t used = overlay_used_blocks(&ov);

        TEST_ASSERT(used == 0 || used == 1);
    }
}

static uint8_t other_mem[BASE_BLOCKS * 512];
static block_device_t other_dev = {"other", 512, BASE_BLOCKS, &base_ops, other_mem};

/* Rebinding to another slot: the old changes are gone, the new base shows through, and it persists. */
static void test_rebind(void)
{
    uint8_t data[512];
    uint8_t back[512];

    setup();
    memset(other_mem, 0x77, sizeof other_mem);
    TEST_ASSERT_EQ_INT(0, overlay_format(&store_dev, &base_dev, 0));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(0));
    pattern(data, 1, 5);
    TEST_ASSERT_EQ_INT(0, block_write(block_device_find("ovl"), 7, 1, data));

    TEST_ASSERT_EQ_INT(0, overlay_rebind(&ov, &other_dev, 1));
    TEST_ASSERT_EQ_INT(0, overlay_used_blocks(&ov));
    TEST_ASSERT_EQ_INT(0, block_read(block_device_find("ovl"), 7, 1, back));
    TEST_ASSERT_EQ_INT(0x77, back[0]);

    TEST_ASSERT_EQ_INT(0, block_write(block_device_find("ovl"), 8, 1, data));

    /* After a "reboot" the overlay belongs to the other base and slot 1. */
    block_reset();
    TEST_ASSERT_EQ_INT(OVERLAY_MISMATCH, overlay_open(&ov, "ovl", &base_dev, &store_dev, 0));
    block_reset();
    TEST_ASSERT_EQ_INT(OVERLAY_OK, overlay_open(&ov, "ovl", &other_dev, &store_dev, 1));
    TEST_ASSERT_EQ_INT(1, overlay_used_blocks(&ov));
    TEST_ASSERT_EQ_INT(0, block_read(block_device_find("ovl"), 8, 1, back));
    TEST_ASSERT(memcmp(back, data, 512) == 0);
    TEST_ASSERT_EQ_INT(0, base_writes);
}

/* Rebinding to a bigger base: the map grows with it. */
static void test_rebind_bigger(void)
{
    static uint8_t big_mem[80 * 512];
    block_device_t big = {"big", 512, 80, &base_ops, big_mem};
    static uint8_t big_store[40 * 512];
    block_device_t store = {"store2", 512, 40, &store_ops, big_store};
    uint8_t data[512];
    uint8_t back[512];

    setup();
    memset(big_mem, 0x3C, sizeof big_mem);
    memset(big_store, 0, sizeof big_store);
    TEST_ASSERT_EQ_INT(0, overlay_format(&store, &base_dev, 0));
    block_reset();
    TEST_ASSERT_EQ_INT(OVERLAY_OK, overlay_open(&ov, "ovl", &base_dev, &store, 0));
    TEST_ASSERT_EQ_INT(0, overlay_rebind(&ov, &big, 1));
    TEST_ASSERT_EQ_INT(80, block_device_find("ovl")->block_count);

    pattern(data, 1, 9);
    TEST_ASSERT_EQ_INT(0, block_write(block_device_find("ovl"), 79, 1, data));
    TEST_ASSERT_EQ_INT(0, block_read(block_device_find("ovl"), 79, 1, back));
    TEST_ASSERT(memcmp(back, data, 512) == 0);
    TEST_ASSERT_EQ_INT(0, block_read(block_device_find("ovl"), 70, 1, back));
    TEST_ASSERT_EQ_INT(0x3C, back[0]);

    block_reset();
    TEST_ASSERT_EQ_INT(OVERLAY_OK, overlay_open(&ov, "ovl", &big, &store, 1));
    TEST_ASSERT_EQ_INT(0, block_read(block_device_find("ovl"), 79, 1, back));
    TEST_ASSERT(memcmp(back, data, 512) == 0);
}

/* A map write that fails: the data block it used is not given out again in this boot. */
static void test_failed_map_write(void)
{
    uint8_t a[512];
    uint8_t b[512];
    uint8_t back[512];

    setup();
    TEST_ASSERT_EQ_INT(0, overlay_format(&store_dev, &base_dev, 0));
    TEST_ASSERT_EQ_INT(OVERLAY_OK, reopen(0));
    pattern(a, 1, 0x10);
    pattern(b, 1, 0x80);

    write_budget = 1;       /* the data block is written, the map block is not */
    TEST_ASSERT_EQ_INT(-1, block_write(block_device_find("ovl"), 20, 1, a));
    write_budget = -1;

    TEST_ASSERT_EQ_INT(0, block_write(block_device_find("ovl"), 30, 1, b));
    TEST_ASSERT_EQ_INT(0, block_read(block_device_find("ovl"), 30, 1, back));
    TEST_ASSERT(memcmp(back, b, 512) == 0);
    TEST_ASSERT_EQ_INT(0, block_read(block_device_find("ovl"), 20, 1, back));
    TEST_ASSERT(memcmp(back, b, 512) != 0);

    /* Map entries past the end of the base are written as 0 (base: 64 blocks, a map block: 128). */
    for (int k = BASE_BLOCKS; k < 128; k++)
        TEST_ASSERT_EQ_INT(0, store_mem[512 + 4 * k] | store_mem[512 + 4 * k + 1] |
                                  store_mem[512 + 4 * k + 2] | store_mem[512 + 4 * k + 3]);
}

int main(void)
{
    TEST_RUN(test_empty_store);
    TEST_RUN(test_read_write);
    TEST_RUN(test_persists);
    TEST_RUN(test_mismatch);
    TEST_RUN(test_reset);
    TEST_RUN(test_full);
    TEST_RUN(test_power_fail);
    TEST_RUN(test_reset_interrupted);
    TEST_RUN(test_rebind);
    TEST_RUN(test_rebind_bigger);
    TEST_RUN(test_failed_map_write);

    return TEST_REPORT("overlay");
}
