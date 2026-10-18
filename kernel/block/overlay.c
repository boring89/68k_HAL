/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "overlay.h"

#include "kernel/lib/k_crc32.h"
#include "kernel/lib/k_memory.h"
#include "kernel/memory/heap.h"

#define OVERLAY_MAGIC_LO 0x4F4B3836u    /* "68KO" read little-endian */
#define OVERLAY_MAGIC_HI 0x3130314Cu    /* "L101" */
#define OVERLAY_MAX_BLOCK 4096

/* Header field offsets. */
#define H_MAGIC 0
#define H_FLAGS 8
#define H_SLOT 12
#define H_BASE_BLOCKS 16
#define H_BASE_CRC 24
#define H_BLOCK_SIZE 28
#define H_MAP_BLOCKS 32
#define H_DATA_BLOCKS 36
#define H_CRC 40        /* CRC-32 of bytes 0..39 */
#define H_SIZE 44

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static uint64_t get64(const uint8_t *p)
{
    return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32;
}

/* Blocks moved per request when the overlay reads or writes many in a row. */
#define CHUNK_BYTES (32 * 1024)

static uint8_t chunk[CHUNK_BYTES];

/* CRC of count blocks from lba, continuing crc. */
static int crc_blocks(block_device_t *dev, uint64_t lba, uint64_t count,
                      uint32_t *crc)
{
    uint32_t per = CHUNK_BYTES / dev->block_size;

    for (uint64_t i = 0; i < count;)
    {
        uint32_t n = count - i < per ? (uint32_t)(count - i) : per;

        if (block_read(dev, lba + i, n, chunk) != 0)
            return -1;

        *crc = k_crc32(*crc, chunk, (size_t)n * dev->block_size);
        i += n;
    }

    return 0;
}

int overlay_identity(block_device_t *base, uint32_t slot,
                     overlay_identity_t *id)
{
    if (base == NULL || id == NULL || base->block_size > OVERLAY_MAX_BLOCK ||
        base->block_count == 0)
        return -1;

    uint64_t span = OVERLAY_IDENTITY_BYTES / base->block_size;

    if (span > base->block_count)
        span = base->block_count;

    uint32_t crc = 0;

    if (crc_blocks(base, 0, span, &crc) != 0 ||
        crc_blocks(base, base->block_count - span, span, &crc) != 0)
        return -1;

    id->base_blocks = base->block_count;
    id->base_crc = crc;
    id->slot = slot;

    return 0;
}

static uint32_t map_blocks_for(const block_device_t *base)
{
    uint64_t per_block = base->block_size / 4;

    return (uint32_t)((base->block_count + per_block - 1) / per_block);
}

static int write_header(block_device_t *store, const overlay_identity_t *id,
                        uint32_t flags, uint32_t map_blocks,
                        uint32_t data_blocks, uint32_t block_size)
{
    uint8_t block[OVERLAY_MAX_BLOCK];

    k_memset(block, 0, store->block_size);
    put32(block + H_MAGIC, OVERLAY_MAGIC_LO);
    put32(block + H_MAGIC + 4, OVERLAY_MAGIC_HI);
    put32(block + H_FLAGS, flags);
    put32(block + H_SLOT, id->slot);
    put64(block + H_BASE_BLOCKS, id->base_blocks);
    put32(block + H_BASE_CRC, id->base_crc);
    put32(block + H_BLOCK_SIZE, block_size);
    put32(block + H_MAP_BLOCKS, map_blocks);
    put32(block + H_DATA_BLOCKS, data_blocks);
    put32(block + H_CRC, k_crc32(0, block, H_CRC));

    return block_write(store, 0, 1, block);
}

static int zero_map(block_device_t *store, uint32_t map_blocks)
{
    uint32_t per = CHUNK_BYTES / store->block_size;

    k_memset(chunk, 0, CHUNK_BYTES);

    for (uint32_t i = 0; i < map_blocks;)
    {
        uint32_t n = map_blocks - i < per ? map_blocks - i : per;

        if (block_write(store, 1 + (uint64_t)i, n, chunk) != 0)
            return -1;

        i += n;
    }

    return 0;
}

static int geometry(block_device_t *store, block_device_t *base,
                    uint32_t *map_blocks, uint32_t *data_blocks)
{
    if (store == NULL || base == NULL || store->block_size != base->block_size ||
        store->block_size > OVERLAY_MAX_BLOCK || store->block_size < 512 ||
        store->ops->write == NULL || base->block_count > 0xFFFFFFFFu)
        return -1;

    uint32_t m = map_blocks_for(base);

    if (store->block_count < 1 + (uint64_t)m + 1)
        return -1;

    uint64_t d = store->block_count - 1 - m;

    *map_blocks = m;
    *data_blocks = d > 0xFFFFFFFEu ? 0xFFFFFFFEu : (uint32_t)d;

    return 0;
}

int overlay_format(block_device_t *store, block_device_t *base, uint32_t slot)
{
    uint32_t map_blocks;
    uint32_t data_blocks;
    overlay_identity_t id;

    if (geometry(store, base, &map_blocks, &data_blocks) != 0 ||
        overlay_identity(base, slot, &id) != 0)
        return -1;

    /* RESETTING first: if the zeroing is cut short, open() finishes it. */
    if (write_header(store, &id, OVERLAY_FLAG_RESETTING, map_blocks,
                     data_blocks, store->block_size) != 0 ||
        zero_map(store, map_blocks) != 0)
        return -1;

    return write_header(store, &id, 0, map_blocks, data_blocks,
                        store->block_size);
}

/* Where a base block's data is, as an index into the store, or -1 */
static uint64_t data_lba(const overlay_t *o, uint32_t entry)
{
    return 1 + (uint64_t)o->map_blocks + (entry - 1);
}

static int overlay_read(block_device_t *device, uint64_t lba, uint32_t count,
                        void *buffer)
{
    overlay_t *o = device->driver_data;
    uint8_t *out = buffer;
    uint32_t i = 0;

    while (i < count)
    {
        uint32_t entry = o->map[lba + i];
        uint32_t run = 1;

        /* Extend over blocks that come from the same place in sequence. */
        while (i + run < count)
        {
            uint32_t next = o->map[lba + i + run];

            if (entry == 0 ? next != 0 : next != entry + run)
                break;

            run++;
        }

        int rc = entry == 0
                     ? block_read(o->base, lba + i, run, out)
                     : block_read(o->store, data_lba(o, entry), run, out);

        if (rc != 0)
            return -1;

        out += (size_t)run * o->block_size;
        i += run;
    }

    return 0;
}

/* Write map block mb from the map in memory (entries past the end of the base are 0). */
static int write_map_block(const overlay_t *o, uint64_t mb, uint8_t *block)
{
    uint32_t per_block = o->block_size / 4;

    for (uint32_t k = 0; k < per_block; k++)
    {
        uint64_t index = mb * per_block + k;

        put32(block + 4 * k, index < o->base->block_count ? o->map[index] : 0);
    }

    return block_write(o->store, 1 + mb, 1, block);
}

static int overlay_write(block_device_t *device, uint64_t lba, uint32_t count,
                         const void *buffer)
{
    overlay_t *o = device->driver_data;
    const uint8_t *in = buffer;
    uint32_t fresh = 0;

    /* Room for the new blocks? */
    for (uint32_t i = 0; i < count; i++)
    {
        if (o->map[lba + i] == 0)
            fresh++;
    }

    if (fresh > o->data_blocks - o->next_free)
        return -1;

    /* Phase 1: the data. New blocks go to consecutive free data blocks. */
    uint32_t target[256];
    uint32_t next = o->next_free;

    for (uint32_t done = 0; done < count;)
    {
        uint32_t n = count - done < 256 ? count - done : 256;

        for (uint32_t i = 0; i < n; i++)
        {
            uint32_t entry = o->map[lba + done + i];

            target[i] = entry != 0 ? entry : ++next;
        }

        for (uint32_t i = 0; i < n;)
        {
            uint32_t run = 1;

            while (i + run < n && target[i + run] == target[i] + run)
                run++;

            if (block_write(o->store, data_lba(o, target[i]), run,
                            in + (size_t)(done + i) * o->block_size) != 0)
                return -1;

            i += run;
        }

        /* The data blocks are taken now, even if writing the map fails below. */
        o->next_free = next;

        /* Phase 2: the map blocks that gained entries, each written once. */
        uint32_t per_block = o->block_size / 4;
        uint64_t last_map = UINT64_MAX;
        uint8_t block[OVERLAY_MAX_BLOCK];

        for (uint32_t i = 0; i < n; i++)
        {
            uint64_t index = lba + done + i;

            if (o->map[index] != 0)
                continue;

            o->map[index] = target[i];
            o->mapped++;

            uint64_t mb = index / per_block;

            if (mb != last_map)
            {
                /* The previous map block is complete: write it. */
                if (last_map != UINT64_MAX && write_map_block(o, last_map, block) != 0)
                    return -1;

                last_map = mb;
            }
        }

        if (last_map != UINT64_MAX && write_map_block(o, last_map, block) != 0)
            return -1;

        done += n;
    }

    return 0;
}

static const block_ops_t overlay_ops = {overlay_read, overlay_write};

overlay_status_t overlay_open(overlay_t *o, const char *name,
                              block_device_t *base, block_device_t *store,
                              uint32_t slot)
{
    uint32_t map_blocks;
    uint32_t data_blocks;
    uint8_t block[OVERLAY_MAX_BLOCK];

    if (o == NULL || name == NULL ||
        geometry(store, base, &map_blocks, &data_blocks) != 0)
        return OVERLAY_ERROR;

    if (block_read(store, 0, 1, block) != 0)
        return OVERLAY_ERROR;

    if (get32(block + H_MAGIC) != OVERLAY_MAGIC_LO ||
        get32(block + H_MAGIC + 4) != OVERLAY_MAGIC_HI ||
        get32(block + H_CRC) != k_crc32(0, block, H_CRC))
        return OVERLAY_EMPTY;

    overlay_identity_t stored;
    uint32_t flags = get32(block + H_FLAGS);

    stored.slot = get32(block + H_SLOT);
    stored.base_blocks = get64(block + H_BASE_BLOCKS);
    stored.base_crc = get32(block + H_BASE_CRC);

    if (get32(block + H_BLOCK_SIZE) != base->block_size ||
        get32(block + H_MAP_BLOCKS) != map_blocks ||
        get32(block + H_DATA_BLOCKS) != data_blocks)
        return OVERLAY_MISMATCH;

    /* An interrupted reset: finish it, then the overlay is empty. */
    if (flags & OVERLAY_FLAG_RESETTING)
    {
        if (zero_map(store, map_blocks) != 0 ||
            write_header(store, &stored, 0, map_blocks, data_blocks,
                         store->block_size) != 0)
            return OVERLAY_ERROR;
    }

    overlay_identity_t now;

    if (overlay_identity(base, slot, &now) != 0)
        return OVERLAY_ERROR;

    if (now.slot != stored.slot || now.base_blocks != stored.base_blocks ||
        now.base_crc != stored.base_crc)
        return OVERLAY_MISMATCH;

    o->map = kmalloc((size_t)base->block_count * 4);

    if (o->map == NULL)
        return OVERLAY_ERROR;

    o->base = base;
    o->store = store;
    o->id = now;
    o->map_blocks = map_blocks;
    o->data_blocks = data_blocks;
    o->block_size = base->block_size;
    o->next_free = 0;
    o->mapped = 0;

    uint32_t per_block = base->block_size / 4;
    uint32_t per_chunk = CHUNK_BYTES / base->block_size;

    for (uint32_t first = 0; first < map_blocks; first += per_chunk)
    {
        uint32_t n = map_blocks - first < per_chunk ? map_blocks - first : per_chunk;

        if (block_read(store, 1 + (uint64_t)first, n, chunk) != 0)
            return OVERLAY_ERROR;

        for (uint32_t k = 0; k < n * per_block; k++)
        {
            uint64_t index = (uint64_t)first * per_block + k;

            if (index >= base->block_count)
                break;

            uint32_t entry = get32(chunk + 4 * k);

            if (entry > data_blocks)
                return OVERLAY_ERROR;

            o->map[index] = entry;

            if (entry != 0)
            {
                o->mapped++;

                if (entry > o->next_free)
                    o->next_free = entry;
            }
        }
    }

    o->device.name = name;
    o->device.block_size = base->block_size;
    o->device.block_count = base->block_count;
    o->device.ops = &overlay_ops;
    o->device.driver_data = o;

    return block_register(&o->device) == 0 ? OVERLAY_OK : OVERLAY_ERROR;
}

int overlay_reset(overlay_t *o)
{
    if (overlay_format(o->store, o->base, o->id.slot) != 0)
        return -1;

    k_memset(o->map, 0, (size_t)o->base->block_count * 4);
    o->next_free = 0;
    o->mapped = 0;

    return 0;
}

int overlay_rebind(overlay_t *o, block_device_t *base, uint32_t slot)
{
    uint32_t map_blocks;
    uint32_t data_blocks;

    if (o == NULL || base == NULL || geometry(o->store, base, &map_blocks, &data_blocks) != 0)
        return -1;

    /* A bigger base needs a bigger map (allocated before anything on the disk changes). */
    uint32_t *map = o->map;

    if (base->block_count > o->base->block_count)
    {
        map = kmalloc((size_t)base->block_count * 4);

        if (map == NULL)
            return -1;
    }

    if (overlay_format(o->store, base, slot) != 0 || overlay_identity(base, slot, &o->id) != 0)
    {
        if (map != o->map)
            kfree(map);
        return -1;
    }

    if (map != o->map)
    {
        kfree(o->map);
        o->map = map;
    }

    k_memset(o->map, 0, (size_t)base->block_count * 4);
    o->base = base;
    o->map_blocks = map_blocks;
    o->data_blocks = data_blocks;
    o->next_free = 0;
    o->mapped = 0;
    o->device.block_count = base->block_count;

    return 0;
}

uint32_t overlay_used_blocks(const overlay_t *o)
{
    return o->mapped;
}

uint32_t overlay_capacity_blocks(const overlay_t *o)
{
    return o->data_blocks;
}

const char *overlay_status_name(overlay_status_t status)
{
    switch (status)
    {
    case OVERLAY_OK:
        return "ok";
    case OVERLAY_EMPTY:
        return "empty";
    case OVERLAY_MISMATCH:
        return "mismatch";
    default:
        return "error";
    }
}
