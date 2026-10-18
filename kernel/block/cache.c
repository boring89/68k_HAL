/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cache.h"

#include "kernel/lib/k_memory.h"
#include "kernel/memory/heap.h"

/*
 * Each line: which device and which line of it (line n = blocks n * per_line ..), its buffer, its
 * place in a hash chain and in the LRU list (head = most recently used). Lines without a device are
 * free; lines without a buffer get one when first used.
 */
typedef struct cache_line
{
    block_device_t *device;
    uint64_t index;
    uint32_t valid_blocks;      /* fewer than a line at the end of a device */
    uint8_t *data;
    struct cache_line *hash_next;
    struct cache_line *lru_prev;
    struct cache_line *lru_next;
} cache_line_t;

static cache_line_t *lines;
static uint32_t line_total;
static cache_line_t **buckets;
static uint32_t bucket_mask;
static cache_line_t *lru_head;
static cache_line_t *lru_tail;
static uint32_t next_unused;        /* lines[next_unused ..] have never been used */
static void *(*line_alloc)(void);
static block_device_t *cached[BLOCK_CACHE_MAX_DEVICES];
static block_cache_stats_t stats;

int block_cache_init(uint64_t max_bytes, void *(*alloc_line)(void))
{
    uint64_t count = max_bytes / BLOCK_CACHE_LINE_BYTES;

    block_cache_reset();

    if (count == 0 || alloc_line == NULL)
        return 0;

    if (count > 0x7FFFFFFFu)
        count = 0x7FFFFFFFu;

    uint32_t buckets_count = 1;

    while (buckets_count < count)
        buckets_count <<= 1;

    lines = kmalloc((size_t)count * sizeof(cache_line_t));
    buckets = kmalloc((size_t)buckets_count * sizeof(cache_line_t *));

    if (lines == NULL || buckets == NULL)
    {
        kfree(lines);
        kfree(buckets);
        lines = NULL;
        buckets = NULL;
        return -1;
    }

    k_memset(lines, 0, (size_t)count * sizeof(cache_line_t));
    k_memset(buckets, 0, (size_t)buckets_count * sizeof(cache_line_t *));
    line_total = (uint32_t)count;
    bucket_mask = buckets_count - 1;
    line_alloc = alloc_line;
    stats.lines_max = line_total;

    return 0;
}

int block_cache_enable(block_device_t *device)
{
    if (line_total == 0 || device == NULL || device->block_size == 0 ||
        BLOCK_CACHE_LINE_BYTES % device->block_size != 0)
        return -1;

    for (int i = 0; i < BLOCK_CACHE_MAX_DEVICES; i++)
    {
        if (cached[i] == device)
            return 0;

        if (cached[i] == NULL)
        {
            cached[i] = device;
            return 0;
        }
    }

    return -1;
}

int block_cache_enabled(const block_device_t *device)
{
    for (int i = 0; i < BLOCK_CACHE_MAX_DEVICES && line_total != 0; i++)
    {
        if (cached[i] == device && device != NULL)
            return 1;
    }

    return 0;
}

static uint32_t hash(const block_device_t *device, uint64_t index)
{
    uint64_t h = index * 0x9E3779B97F4A7C15ull ^ (uint64_t)(uintptr_t)device;

    return (uint32_t)(h ^ (h >> 29)) & bucket_mask;
}

static void lru_unlink(cache_line_t *l)
{
    if (l->lru_prev != NULL)
        l->lru_prev->lru_next = l->lru_next;
    else
        lru_head = l->lru_next;

    if (l->lru_next != NULL)
        l->lru_next->lru_prev = l->lru_prev;
    else
        lru_tail = l->lru_prev;

    l->lru_prev = l->lru_next = NULL;
}

static void lru_push_front(cache_line_t *l)
{
    l->lru_prev = NULL;
    l->lru_next = lru_head;

    if (lru_head != NULL)
        lru_head->lru_prev = l;

    lru_head = l;

    if (lru_tail == NULL)
        lru_tail = l;
}

static void hash_remove(cache_line_t *l)
{
    cache_line_t **p = &buckets[hash(l->device, l->index)];

    while (*p != NULL && *p != l)
        p = &(*p)->hash_next;

    if (*p == l)
        *p = l->hash_next;

    l->hash_next = NULL;
}

static cache_line_t *lookup(const block_device_t *device, uint64_t index)
{
    for (cache_line_t *l = buckets[hash(device, index)]; l != NULL; l = l->hash_next)
    {
        if (l->device == device && l->index == index)
            return l;
    }

    return NULL;
}

/* An empty line goes to the tail of the LRU list: it is the first to be reused. */
static void park_empty(cache_line_t *l)
{
    l->device = NULL;
    l->lru_prev = lru_tail;
    l->lru_next = NULL;

    if (lru_tail != NULL)
        lru_tail->lru_next = l;
    else
        lru_head = l;

    lru_tail = l;
}

/* A line to fill: a never used one (with a new buffer) while memory lasts, else the least recently used. */
static cache_line_t *take_line(void)
{
    if (next_unused < line_total)
    {
        void *data = line_alloc();

        if (data != NULL)
        {
            cache_line_t *l = &lines[next_unused++];

            l->data = data;
            stats.lines_used++;
            return l;
        }

        next_unused = line_total;   /* out of memory: from now on lines are reused */
    }

    cache_line_t *l = lru_tail;

    if (l == NULL)
        return NULL;

    lru_unlink(l);
    hash_remove(l);
    l->device = NULL;
    stats.evictions++;

    return l;
}

/* The line holding block lba's line of device, read from the disk if needed; NULL on an I/O error. */
static cache_line_t *get_line(block_device_t *device, uint64_t index)
{
    cache_line_t *l = lookup(device, index);

    if (l != NULL)
    {
        stats.hits++;
        lru_unlink(l);
        lru_push_front(l);
        return l;
    }

    stats.misses++;
    l = take_line();

    if (l == NULL)
        return NULL;

    uint32_t per_line = BLOCK_CACHE_LINE_BYTES / device->block_size;
    uint64_t first = index * per_line;
    uint32_t count = device->block_count - first < per_line ? (uint32_t)(device->block_count - first) : per_line;

    if (device->ops->read(device, first, count, l->data) != 0)
    {
        park_empty(l);
        return NULL;
    }

    l->device = device;
    l->index = index;
    l->valid_blocks = count;
    l->hash_next = buckets[hash(device, index)];
    buckets[hash(device, index)] = l;
    lru_push_front(l);

    return l;
}

int block_cache_read(block_device_t *device, uint64_t lba, uint32_t count, void *buffer)
{
    uint32_t per_line = BLOCK_CACHE_LINE_BYTES / device->block_size;
    uint8_t *out = buffer;

    while (count > 0)
    {
        uint64_t index = lba / per_line;
        uint32_t offset = (uint32_t)(lba % per_line);
        uint32_t n = per_line - offset < count ? per_line - offset : count;
        cache_line_t *l = get_line(device, index);

        if (l == NULL)
        {
            /* No line to be had (or a read error inside it): straight from the disk. */
            if (device->ops->read(device, lba, n, out) != 0)
                return -1;
        }
        else
        {
            k_memcpy(out, l->data + (size_t)offset * device->block_size, (size_t)n * device->block_size);
        }

        out += (size_t)n * device->block_size;
        lba += n;
        count -= n;
    }

    return 0;
}

int block_cache_write(block_device_t *device, uint64_t lba, uint32_t count, const void *buffer)
{
    uint32_t per_line = BLOCK_CACHE_LINE_BYTES / device->block_size;
    const uint8_t *in = buffer;

    stats.writes++;

    /* The disk first: when this returns, the data is on the medium (block.h). */
    if (device->ops->write(device, lba, count, buffer) != 0)
    {
        /* What the disk now holds is unknown: forget the lines of the range. */
        for (uint64_t index = lba / per_line; index <= (lba + count - 1) / per_line; index++)
        {
            cache_line_t *l = lookup(device, index);

            if (l != NULL)
            {
                hash_remove(l);
                lru_unlink(l);
                park_empty(l);
            }
        }

        return -1;
    }

    /* Then the lines that are cached; lines that are not are left uncached. */
    while (count > 0)
    {
        uint64_t index = lba / per_line;
        uint32_t offset = (uint32_t)(lba % per_line);
        uint32_t n = per_line - offset < count ? per_line - offset : count;
        cache_line_t *l = lookup(device, index);

        if (l != NULL)
            k_memcpy(l->data + (size_t)offset * device->block_size, in, (size_t)n * device->block_size);

        in += (size_t)n * device->block_size;
        lba += n;
        count -= n;
    }

    return 0;
}

void block_cache_drop(void)
{
    for (uint32_t i = 0; i < line_total; i++)
    {
        lines[i].device = NULL;
        lines[i].hash_next = NULL;
    }

    for (uint32_t i = 0; i <= bucket_mask && buckets != NULL; i++)
        buckets[i] = NULL;

    /* Every line with a buffer goes back on the LRU list, empty, to be reused. */
    lru_head = lru_tail = NULL;

    for (uint32_t i = 0; i < next_unused && i < line_total; i++)
    {
        if (lines[i].data != NULL)
        {
            lines[i].lru_prev = lines[i].lru_next = NULL;
            lru_push_front(&lines[i]);
        }
    }
}

void block_cache_get_stats(block_cache_stats_t *out)
{
    *out = stats;
}

void block_cache_reset(void)
{
    kfree(lines);
    kfree(buckets);
    lines = NULL;
    buckets = NULL;
    line_total = 0;
    bucket_mask = 0;
    lru_head = lru_tail = NULL;
    next_unused = 0;
    line_alloc = NULL;
    k_memset(cached, 0, sizeof cached);
    k_memset(&stats, 0, sizeof stats);
}
