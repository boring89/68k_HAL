/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>

#include "kernel/memory/heap.h"

#define REGION_SIZE 1024
#define MAX_BLOCKS 64

static _Alignas(8) uint8_t region[REGION_SIZE];

typedef struct
{
    size_t used;
    uintptr_t current;
    int count;
    heap_block_info_t blocks[MAX_BLOCKS];

} heap_snapshot_t;

static void reset(void)
{
    heap_init((uintptr_t)region, sizeof(region));
}

/* Header size as seen from the first block (alloc one if empty). */
static size_t header_size(void)
{
    heap_block_info_t info;

    TEST_ASSERT_EQ_INT(0, heap_get_block(0, &info));

    return info.address - info.header;
}

static int block_count(void)
{
    heap_block_info_t info;
    int i = 0;

    while (heap_get_block(i, &info) == 0)
        i++;

    return i;
}

static void take_snapshot(heap_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));

    snapshot->used = heap_get_used();
    snapshot->current = heap_get_current();

    while (snapshot->count < MAX_BLOCKS &&
           heap_get_block(snapshot->count,
                          &snapshot->blocks[snapshot->count]) == 0)
    {
        snapshot->count++;
    }
}

static int snapshot_equal(const heap_snapshot_t *a, const heap_snapshot_t *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static void test_init_aligns_start(void)
{
    heap_init((uintptr_t)region + 3, 100);

    TEST_ASSERT_EQ_INT((uintptr_t)region + 8, heap_get_start());
    TEST_ASSERT_EQ_INT(heap_get_start(), heap_get_current());
    TEST_ASSERT_EQ_INT(0, heap_get_used());

    void *p = kmalloc(1);
    TEST_ASSERT(p != 0);
    TEST_ASSERT_EQ_INT(0, (uintptr_t)p % 8);

    /* Region smaller than the alignment padding: nothing fits. */
    heap_init((uintptr_t)region + 3, 4);
    TEST_ASSERT(kmalloc(1) == 0);
}

static void test_alignment(void)
{
    reset();

    void *a = kmalloc(1);
    void *b = kmalloc(3);
    void *c = kmalloc(13);

    TEST_ASSERT_EQ_INT(0, (uintptr_t)a % 8);
    TEST_ASSERT_EQ_INT(0, (uintptr_t)b % 8);
    TEST_ASSERT_EQ_INT(0, (uintptr_t)c % 8);
    TEST_ASSERT_EQ_INT(0, header_size() % 8);

    heap_block_info_t info;

    heap_get_block(0, &info);
    TEST_ASSERT_EQ_INT(8, info.size);
    TEST_ASSERT_EQ_INT(0, info.header % 8);

    heap_get_block(2, &info);
    TEST_ASSERT_EQ_INT(16, info.size);

    TEST_ASSERT_EQ_INT(8 + 8 + 16, heap_get_used());
}

static void test_alloc_free(void)
{
    reset();

    TEST_ASSERT(kmalloc(0) == 0);

    void *a = kmalloc(16);
    void *b = kmalloc(32);

    TEST_ASSERT(a != 0);
    TEST_ASSERT(b != 0);
    TEST_ASSERT(a != b);
    TEST_ASSERT_EQ_INT(48, heap_get_used());

    /* Payloads must not overlap. */
    memset(a, 0xAA, 16);
    memset(b, 0xBB, 32);
    TEST_ASSERT_EQ_INT(0xAA, ((uint8_t *)a)[15]);

    TEST_ASSERT_EQ_INT(0, kfree(a));
    TEST_ASSERT_EQ_INT(32, heap_get_used());

    TEST_ASSERT_EQ_INT(0, kfree(b));
    TEST_ASSERT_EQ_INT(0, heap_get_used());
}

static void test_merge_adjacent(void)
{
    reset();

    void *a = kmalloc(16);
    void *b = kmalloc(24);
    void *c = kmalloc(32);
    void *guard = kmalloc(8);
    size_t header = header_size();

    TEST_ASSERT_EQ_INT(4, block_count());

    kfree(a);
    kfree(c);
    TEST_ASSERT_EQ_INT(4, block_count());

    /* Freeing b joins a, b and c into one block. */
    kfree(b);
    TEST_ASSERT_EQ_INT(2, block_count());

    heap_block_info_t info;

    heap_get_block(0, &info);
    TEST_ASSERT_EQ_INT(0, info.used);
    TEST_ASSERT_EQ_INT(16 + 24 + 32 + 2 * header, info.size);
    TEST_ASSERT_EQ_INT(8, heap_get_used());

    /* The merged block can satisfy a request none of the parts could. */
    void *big = kmalloc(16 + 24 + 32 + 2 * header);
    TEST_ASSERT(big == a);

    kfree(big);
    kfree(guard);
}

static void test_trailing_free_released(void)
{
    reset();

    void *a = kmalloc(16);
    void *b = kmalloc(32);
    uintptr_t after_a = heap_get_current() - 32 - header_size();

    kfree(b);
    TEST_ASSERT_EQ_INT(after_a, heap_get_current());
    TEST_ASSERT_EQ_INT(1, block_count());

    kfree(a);
    TEST_ASSERT_EQ_INT(heap_get_start(), heap_get_current());
    TEST_ASSERT_EQ_INT(0, heap_get_reserved());
    TEST_ASSERT_EQ_INT(0, heap_get_used());
}

static void test_reuse_same_size(void)
{
    reset();

    void *a = kmalloc(16);
    void *guard = kmalloc(8);
    uintptr_t current = heap_get_current();

    kfree(a);

    void *c = kmalloc(16);
    TEST_ASSERT(c == a);
    TEST_ASSERT_EQ_INT(current, heap_get_current());
    TEST_ASSERT_EQ_INT(24, heap_get_used());

    kfree(c);
    kfree(guard);
}

static void test_reuse_without_split(void)
{
    reset();

    /* Leftover of 8 bytes is too small for a header: no split. */
    void *a = kmalloc(16);
    void *guard = kmalloc(8);

    kfree(a);

    void *c = kmalloc(8);
    TEST_ASSERT(c == a);

    heap_block_info_t info;
    heap_get_block(0, &info);
    TEST_ASSERT_EQ_INT(16, info.size);

    /* Accounting uses the real block size on both sides. */
    TEST_ASSERT_EQ_INT(16 + 8, heap_get_used());

    kfree(c);
    TEST_ASSERT_EQ_INT(8, heap_get_used());

    kfree(guard);
    TEST_ASSERT_EQ_INT(0, heap_get_used());
}

static void test_reuse_with_split(void)
{
    reset();

    void *a = kmalloc(64);
    void *guard = kmalloc(8);
    size_t header = header_size();

    kfree(a);

    void *c = kmalloc(8);
    TEST_ASSERT(c == a);
    TEST_ASSERT_EQ_INT(3, block_count());

    heap_block_info_t info;

    heap_get_block(0, &info);
    TEST_ASSERT_EQ_INT(8, info.size);
    TEST_ASSERT_EQ_INT(1, info.used);

    heap_get_block(1, &info);
    TEST_ASSERT_EQ_INT(64 - 8 - header, info.size);
    TEST_ASSERT_EQ_INT(0, info.used);

    TEST_ASSERT_EQ_INT(8 + 8, heap_get_used());

    kfree(c);
    TEST_ASSERT_EQ_INT(2, block_count());
    TEST_ASSERT_EQ_INT(8, heap_get_used());

    kfree(guard);
}

static void test_limit(void)
{
    reset();

    TEST_ASSERT(kmalloc(REGION_SIZE) == 0);
    TEST_ASSERT(kmalloc(SIZE_MAX) == 0);
    TEST_ASSERT(kmalloc(SIZE_MAX - 3) == 0);
    TEST_ASSERT_EQ_INT(0, heap_get_reserved());

    void *first = kmalloc(8);
    size_t header = header_size();
    kfree(first);

    /* Exactly fills the region. */
    void *all = kmalloc(REGION_SIZE - header);
    TEST_ASSERT(all != 0);
    TEST_ASSERT_EQ_INT(REGION_SIZE, heap_get_reserved());
    TEST_ASSERT_EQ_INT((uintptr_t)region + REGION_SIZE, heap_get_current());

    TEST_ASSERT(kmalloc(8) == 0);
    TEST_ASSERT_EQ_INT(REGION_SIZE - header, heap_get_used());

    kfree(all);

    /* One byte too many after the header. */
    TEST_ASSERT(kmalloc(REGION_SIZE - header + 1) == 0);
    TEST_ASSERT_EQ_INT(0, heap_get_used());
}

static void test_kfree_invalid(void)
{
    reset();

    uint8_t *a = kmalloc(32);
    uint8_t *b = kmalloc(16);
    void *guard = kmalloc(8);

    heap_snapshot_t before;
    heap_snapshot_t after;

    take_snapshot(&before);

    TEST_ASSERT_EQ_INT(-1, kfree(0));
    TEST_ASSERT_EQ_INT(-1, kfree((void *)((uintptr_t)region - 8)));
    TEST_ASSERT_EQ_INT(-1, kfree(region));
    TEST_ASSERT_EQ_INT(-1, kfree(region + REGION_SIZE - 8));
    TEST_ASSERT_EQ_INT(-1, kfree(a + 1));
    TEST_ASSERT_EQ_INT(-1, kfree(a + 8));
    TEST_ASSERT_EQ_INT(-1, kfree(a + 16));

    take_snapshot(&after);
    TEST_ASSERT(snapshot_equal(&before, &after));

    /* Double free. */
    TEST_ASSERT_EQ_INT(0, kfree(b));
    take_snapshot(&before);
    TEST_ASSERT_EQ_INT(-1, kfree(b));
    take_snapshot(&after);
    TEST_ASSERT(snapshot_equal(&before, &after));

    /* b's old header was absorbed by the merge and reallocated over. */
    TEST_ASSERT_EQ_INT(0, kfree(a));
    void *big = kmalloc(32 + 16 + header_size());
    TEST_ASSERT(big == a);

    take_snapshot(&before);
    TEST_ASSERT_EQ_INT(-1, kfree(b));
    take_snapshot(&after);
    TEST_ASSERT(snapshot_equal(&before, &after));

    kfree(big);
    kfree(guard);
    TEST_ASSERT_EQ_INT(0, heap_get_used());
    TEST_ASSERT_EQ_INT(0, heap_get_reserved());
}

static void test_used_consistent(void)
{
    void *ptrs[8];
    size_t sizes[8] = {8, 24, 40, 8, 16, 72, 8, 32};

    reset();

    for (int i = 0; i < 8; i++)
        ptrs[i] = kmalloc(sizes[i]);

    TEST_ASSERT_EQ_INT(208, heap_get_used());

    /* Free every other block, then reuse the holes with smaller sizes. */
    for (int i = 0; i < 8; i += 2)
        kfree(ptrs[i]);

    TEST_ASSERT_EQ_INT(24 + 8 + 72 + 32, heap_get_used());

    for (int i = 0; i < 8; i += 2)
        ptrs[i] = kmalloc(8);

    /* Sum of block sizes reported by heap_get_block must match. */
    heap_block_info_t info;
    size_t total = 0;

    for (int i = 0; heap_get_block(i, &info) == 0; i++)
    {
        if (info.used)
            total += info.size;
    }

    TEST_ASSERT_EQ_INT(total, heap_get_used());

    for (int i = 0; i < 8; i++)
        kfree(ptrs[i]);

    TEST_ASSERT_EQ_INT(0, heap_get_used());
    TEST_ASSERT_EQ_INT(0, heap_get_reserved());
}

/*
 * Growth with a fake callback: "memory" is a big static array and the
 * callback only has to say how far the usable part of it now reaches.
 */
#define BIG_SIZE (64 * 1024)
static _Alignas(8) uint8_t big_region[BIG_SIZE];
static int grow_calls;
static size_t grow_step = 256;
static int grow_fail;
static uintptr_t grow_last_address;

static size_t fake_grow(uintptr_t address, size_t min_bytes)
{
    grow_calls++;
    grow_last_address = address;

    if (grow_fail)
        return 0;

    size_t bytes = (min_bytes + grow_step - 1) / grow_step * grow_step;

    TEST_ASSERT(address + bytes <= (uintptr_t)big_region + BIG_SIZE);
    return bytes;
}

static void grow_reset(size_t initial)
{
    grow_calls = 0;
    grow_fail = 0;
    heap_init((uintptr_t)big_region, initial);
    heap_set_grow(fake_grow, (uintptr_t)big_region + BIG_SIZE);
}

static void test_grow_on_demand(void)
{
    grow_reset(0);

    void *a = kmalloc(100);

    TEST_ASSERT(a != 0);
    TEST_ASSERT_EQ_INT(1, grow_calls);
    TEST_ASSERT_EQ_INT((uintptr_t)big_region, grow_last_address);

    /* Fits in what was already granted: no second call. */
    void *b = kmalloc(32);

    TEST_ASSERT(b != 0);
    TEST_ASSERT_EQ_INT(1, grow_calls);

    /* The next call is asked to start where the heap now ends. */
    void *c = kmalloc(1000);

    TEST_ASSERT(c != 0);
    TEST_ASSERT_EQ_INT(2, grow_calls);
    TEST_ASSERT_EQ_INT((uintptr_t)big_region + 256, grow_last_address);

    /* All three are distinct, usable and contiguous in the one range. */
    memset(a, 1, 100);
    memset(b, 2, 32);
    memset(c, 3, 1000);
    TEST_ASSERT_EQ_INT(1, ((uint8_t *)a)[99]);
    TEST_ASSERT_EQ_INT(2, ((uint8_t *)b)[0]);
    TEST_ASSERT(heap_get_reserved() >= 100 + 32 + 1000);
}

static void test_grow_failure_and_limit(void)
{
    grow_reset(0);
    grow_fail = 1;
    TEST_ASSERT(kmalloc(8) == 0);
    TEST_ASSERT_EQ_INT(1, grow_calls);

    /* A request over the limit is refused without asking the callback. */
    grow_reset(0);
    heap_set_grow(fake_grow, (uintptr_t)big_region + 512);
    TEST_ASSERT(kmalloc(1024) == 0);
    TEST_ASSERT_EQ_INT(0, grow_calls);
    TEST_ASSERT(kmalloc(256) != 0);

    /* A callback with byte granularity works too. */
    grow_reset(0);
    grow_step = 1;
    TEST_ASSERT(kmalloc(8) != 0);
    grow_step = 256;
}

static void test_grow_partly_used_region(void)
{
    /* Start with 64 bytes; a 200-byte request needs more than that. */
    grow_reset(64);

    void *a = kmalloc(8);

    TEST_ASSERT(a != 0);
    TEST_ASSERT_EQ_INT(0, grow_calls);

    void *b = kmalloc(200);

    TEST_ASSERT(b != 0);
    TEST_ASSERT_EQ_INT(1, grow_calls);
    TEST_ASSERT_EQ_INT((uintptr_t)big_region + 64, grow_last_address);

    /* Without a callback the old behaviour is unchanged. */
    heap_set_grow(0, 0);
    reset();
    TEST_ASSERT(kmalloc(REGION_SIZE) == 0);
}

int main(void)
{
    TEST_RUN(test_init_aligns_start);
    TEST_RUN(test_alignment);
    TEST_RUN(test_alloc_free);
    TEST_RUN(test_merge_adjacent);
    TEST_RUN(test_trailing_free_released);
    TEST_RUN(test_reuse_same_size);
    TEST_RUN(test_reuse_without_split);
    TEST_RUN(test_reuse_with_split);
    TEST_RUN(test_limit);
    TEST_RUN(test_kfree_invalid);
    TEST_RUN(test_used_consistent);
    TEST_RUN(test_grow_on_demand);
    TEST_RUN(test_grow_failure_and_limit);
    TEST_RUN(test_grow_partly_used_region);

    return TEST_REPORT("heap");
}
