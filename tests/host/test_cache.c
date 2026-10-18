/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>

#include "kernel/block/block.h"
#include "kernel/block/cache.h"
#include "kernel/block/overlay.h"
#include "kernel/memory/heap.h"

#define DISK_BLOCKS 1000            /* not a multiple of a line (64 blocks): a short last line */
#define PER_LINE (BLOCK_CACHE_LINE_BYTES / 512)

static uint8_t disk_mem[DISK_BLOCKS * 512];
static uint8_t arena[512 * 1024] __attribute__((aligned(16)));
static int driver_reads;
static int driver_writes;
static int fail_writes;
static int write_budget = -1;       /* single-block writes before the "power fails" */
static uint64_t write_log[64];
static int write_log_count;
static int lines_allowed;

static int disk_read(block_device_t *d, uint64_t lba, uint32_t count, void *buf)
{
    (void)d;
    driver_reads++;
    memcpy(buf, disk_mem + lba * 512, (size_t)count * 512);
    return 0;
}

static int disk_write(block_device_t *d, uint64_t lba, uint32_t count, const void *buf)
{
    (void)d;
    if (fail_writes)
        return -1;

    for (uint32_t i = 0; i < count; i++)
    {
        if (write_budget == 0)
            return -1;
        if (write_budget > 0)
            write_budget--;
        memcpy(disk_mem + (lba + i) * 512, (const uint8_t *)buf + (size_t)i * 512, 512);
    }

    driver_writes++;
    if (write_log_count < 64)
        write_log[write_log_count++] = lba;
    return 0;
}

static const block_ops_t ops = {disk_read, disk_write};
static block_device_t disk = {"disk", 512, DISK_BLOCKS, &ops, NULL};

/* Line buffers from a static pool, given out again after every setup (the cache never frees them). */
#define POOL_LINES 32
static uint8_t pool[POOL_LINES][BLOCK_CACHE_LINE_BYTES];
static int pool_next;

static void *alloc_line(void)
{
    if (lines_allowed == 0 || pool_next == POOL_LINES)
        return NULL;
    lines_allowed--;
    return pool[pool_next++];
}

static void setup(uint32_t lines, int allowed)
{
    block_reset();
    heap_init((uintptr_t)arena, sizeof(arena));
    for (size_t i = 0; i < sizeof disk_mem; i++)
        disk_mem[i] = (uint8_t)(i * 13 + i / 512);
    driver_reads = driver_writes = fail_writes = 0;
    write_budget = -1;
    write_log_count = 0;
    lines_allowed = allowed;
    pool_next = 0;
    TEST_ASSERT_EQ_INT(0, block_cache_init((uint64_t)lines * BLOCK_CACHE_LINE_BYTES, alloc_line));
    TEST_ASSERT_EQ_INT(0, block_register(&disk));
    TEST_ASSERT_EQ_INT(0, block_cache_enable(&disk));
}

static void test_hits_and_misses(void)
{
    uint8_t buf[4 * 512];
    block_cache_stats_t st;

    setup(4, 100);
    TEST_ASSERT(block_cache_enabled(&disk));

    /* The first read brings the whole line in, the next ones in it cost nothing. */
    TEST_ASSERT_EQ_INT(0, block_read(&disk, 3, 2, buf));
    TEST_ASSERT_EQ_INT(0, memcmp(buf, disk_mem + 3 * 512, 2 * 512));
    TEST_ASSERT_EQ_INT(1, driver_reads);
    TEST_ASSERT_EQ_INT(0, block_read(&disk, 60, 4, buf));
    TEST_ASSERT_EQ_INT(0, memcmp(buf, disk_mem + 60 * 512, 4 * 512));
    TEST_ASSERT_EQ_INT(1, driver_reads);

    /* Across a line boundary: one more line. */
    TEST_ASSERT_EQ_INT(0, block_read(&disk, PER_LINE - 2, 4, buf));
    TEST_ASSERT_EQ_INT(0, memcmp(buf, disk_mem + (PER_LINE - 2) * 512, 4 * 512));
    TEST_ASSERT_EQ_INT(2, driver_reads);

    block_cache_get_stats(&st);
    TEST_ASSERT_EQ_INT(2, st.misses);
    TEST_ASSERT_EQ_INT(2, st.hits);
    TEST_ASSERT_EQ_INT(2, st.lines_used);
    TEST_ASSERT_EQ_INT(4, st.lines_max);

    /* The short last line (1000 = 15 * 64 + 40). */
    TEST_ASSERT_EQ_INT(0, block_read(&disk, DISK_BLOCKS - 1, 1, buf));
    TEST_ASSERT_EQ_INT(0, memcmp(buf, disk_mem + (DISK_BLOCKS - 1) * 512, 512));
    TEST_ASSERT_EQ_INT(-1, block_read(&disk, DISK_BLOCKS, 1, buf));
}

static void test_lru(void)
{
    uint8_t buf[512];
    block_cache_stats_t st;

    setup(2, 100);
    TEST_ASSERT_EQ_INT(0, block_read(&disk, 0 * PER_LINE, 1, buf));   /* line 0 */
    TEST_ASSERT_EQ_INT(0, block_read(&disk, 1 * PER_LINE, 1, buf));   /* line 1 */
    TEST_ASSERT_EQ_INT(0, block_read(&disk, 0 * PER_LINE, 1, buf));   /* line 0 again: most recent */
    TEST_ASSERT_EQ_INT(0, block_read(&disk, 2 * PER_LINE, 1, buf));   /* evicts line 1 */
    TEST_ASSERT_EQ_INT(3, driver_reads);
    TEST_ASSERT_EQ_INT(0, block_read(&disk, 0 * PER_LINE, 1, buf));   /* still cached */
    TEST_ASSERT_EQ_INT(3, driver_reads);
    TEST_ASSERT_EQ_INT(0, block_read(&disk, 1 * PER_LINE, 1, buf));   /* was evicted */
    TEST_ASSERT_EQ_INT(4, driver_reads);
    TEST_ASSERT_EQ_INT(0, memcmp(buf, disk_mem + PER_LINE * 512, 512));
    block_cache_get_stats(&st);
    TEST_ASSERT_EQ_INT(2, st.evictions);

    /* Memory runs out after one line: that line is reused. */
    setup(8, 1);
    TEST_ASSERT_EQ_INT(0, block_read(&disk, 0, 1, buf));
    TEST_ASSERT_EQ_INT(0, block_read(&disk, PER_LINE, 1, buf));
    TEST_ASSERT_EQ_INT(0, memcmp(buf, disk_mem + PER_LINE * 512, 512));
    TEST_ASSERT_EQ_INT(0, block_read(&disk, 0, 1, buf));
    TEST_ASSERT_EQ_INT(0, memcmp(buf, disk_mem, 512));
    block_cache_get_stats(&st);
    TEST_ASSERT_EQ_INT(1, st.lines_used);

    /* No memory at all: reads go to the disk. */
    setup(8, 0);
    TEST_ASSERT_EQ_INT(0, block_read(&disk, 5, 1, buf));
    TEST_ASSERT_EQ_INT(0, memcmp(buf, disk_mem + 5 * 512, 512));
}

static void test_write_through(void)
{
    uint8_t data[3 * 512];
    uint8_t buf[3 * 512];

    setup(4, 100);
    TEST_ASSERT_EQ_INT(0, block_read(&disk, 0, 1, buf));       /* line 0 cached */
    memset(data, 0xA7, sizeof data);

    /* The disk has it at once, in the order of the calls; the cached line too. */
    TEST_ASSERT_EQ_INT(0, block_write(&disk, 10, 1, data));
    TEST_ASSERT_EQ_INT(0, block_write(&disk, 200, 1, data));   /* line 3: not cached, stays uncached */
    TEST_ASSERT_EQ_INT(0, block_write(&disk, 5, 1, data));
    TEST_ASSERT_EQ_INT(3, write_log_count);
    TEST_ASSERT_EQ_INT(10, write_log[0]);
    TEST_ASSERT_EQ_INT(200, write_log[1]);
    TEST_ASSERT_EQ_INT(5, write_log[2]);
    TEST_ASSERT_EQ_INT(0xA7, disk_mem[10 * 512]);
    int reads = driver_reads;

    TEST_ASSERT_EQ_INT(0, block_read(&disk, 10, 1, buf));
    TEST_ASSERT_EQ_INT(0xA7, buf[0]);
    TEST_ASSERT_EQ_INT(reads, driver_reads);

    /* A write across two lines, one cached. */
    TEST_ASSERT_EQ_INT(0, block_write(&disk, PER_LINE - 1, 3, data));
    TEST_ASSERT_EQ_INT(0, block_read(&disk, PER_LINE - 1, 3, buf));
    TEST_ASSERT_EQ_INT(0, memcmp(buf, data, sizeof data));

    /* A failed write: the cached line is forgotten (the disk may hold either). */
    fail_writes = 1;
    memset(data, 0x11, sizeof data);
    TEST_ASSERT_EQ_INT(-1, block_write(&disk, 12, 1, data));
    fail_writes = 0;
    reads = driver_reads;
    TEST_ASSERT_EQ_INT(0, block_read(&disk, 12, 1, buf));
    TEST_ASSERT_EQ_INT(reads + 1, driver_reads);
    TEST_ASSERT_EQ_INT(0, memcmp(buf, disk_mem + 12 * 512, 512));

    /* drop: everything is read again. */
    block_cache_drop();
    reads = driver_reads;
    TEST_ASSERT_EQ_INT(0, block_read(&disk, 10, 1, buf));
    TEST_ASSERT_EQ_INT(reads + 1, driver_reads);
    TEST_ASSERT_EQ_INT(0xA7, buf[0]);
}

static void test_not_enabled(void)
{
    uint8_t buf[512];
    block_device_t other = {"other", 512, DISK_BLOCKS, &ops, NULL};

    setup(4, 100);
    TEST_ASSERT_EQ_INT(0, block_register(&other));
    TEST_ASSERT(!block_cache_enabled(&other));
    TEST_ASSERT_EQ_INT(0, block_read(&other, 0, 1, buf));
    TEST_ASSERT_EQ_INT(0, block_read(&other, 0, 1, buf));
    TEST_ASSERT_EQ_INT(2, driver_reads);

    /* No cache at all: enabling fails. */
    block_reset();
    TEST_ASSERT_EQ_INT(-1, block_cache_enable(&disk));
}

/* The base of the overlay test: separate memory, read only, not cached. */
static uint8_t base_mem[64 * 512];

static int base_read(block_device_t *d, uint64_t lba, uint32_t count, void *buf)
{
    (void)d;
    memcpy(buf, base_mem + lba * 512, (size_t)count * 512);
    return 0;
}

static const block_ops_t base_ops = {base_read, NULL};
static block_device_t base = {"base", 512, 64, &base_ops, NULL};

/* The overlay's store on a cached disk: cutting the power after every write keeps its promise (D2). */
static void test_overlay_power_fail(void)
{
    static overlay_t ov;
    uint8_t first[512];
    uint8_t second[3 * 512];
    uint8_t back[512];

    memset(first, 0x21, sizeof first);
    memset(second, 0x42, sizeof second);
    for (size_t i = 0; i < sizeof base_mem; i++)
        base_mem[i] = (uint8_t)(i * 7);

    for (int budget = 0; budget < 8; budget++)
    {
        setup(16, 100);
        memset(disk_mem, 0, sizeof disk_mem);
        TEST_ASSERT_EQ_INT(0, block_register(&base));
        TEST_ASSERT_EQ_INT(0, overlay_format(&disk, &base, 0));
        TEST_ASSERT_EQ_INT(OVERLAY_OK, overlay_open(&ov, "ovl", &base, &disk, 0));
        TEST_ASSERT_EQ_INT(0, block_write(block_device_find("ovl"), 8, 1, first));

        write_budget = budget;
        int rc = block_write(block_device_find("ovl"), 9, 3, second);

        write_budget = -1;

        /* Reboot: the cache is gone, only what reached the disk counts. */
        block_reset();
        heap_init((uintptr_t)arena, sizeof(arena));
        lines_allowed = 100;
        pool_next = 0;
        TEST_ASSERT_EQ_INT(0, block_cache_init(16 * BLOCK_CACHE_LINE_BYTES, alloc_line));
        TEST_ASSERT_EQ_INT(0, block_register(&disk));
        TEST_ASSERT_EQ_INT(0, block_register(&base));
        TEST_ASSERT_EQ_INT(0, block_cache_enable(&disk));
        TEST_ASSERT_EQ_INT(OVERLAY_OK, overlay_open(&ov, "ovl", &base, &disk, 0));
        TEST_ASSERT_EQ_INT(0, block_read(block_device_find("ovl"), 8, 1, back));
        TEST_ASSERT(memcmp(back, first, 512) == 0);

        for (int b = 0; b < 3; b++)
        {
            TEST_ASSERT_EQ_INT(0, block_read(block_device_find("ovl"), 9 + b, 1, back));

            int old = memcmp(back, base_mem + (9 + b) * 512, 512) == 0;
            int upd = memcmp(back, second + b * 512, 512) == 0;

            TEST_ASSERT(old || upd);
            if (rc == 0)
                TEST_ASSERT(upd);
        }
    }
}

int main(void)
{
    TEST_RUN(test_hits_and_misses);
    TEST_RUN(test_lru);
    TEST_RUN(test_write_through);
    TEST_RUN(test_not_enabled);
    TEST_RUN(test_overlay_power_fail);

    block_reset();
    return TEST_REPORT("cache");
}
