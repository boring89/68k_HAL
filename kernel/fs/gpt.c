/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gpt.h"

#include "kernel/lib/k_crc32.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"

/* Header layout (UEFI Specification 2.10, table 5.5). */
#define GPT_SIGNATURE_LO 0x20494645u /* "EFI " */
#define GPT_SIGNATURE_HI 0x54524150u /* "PART" */
#define HDR_OFFSET_SIZE 12
#define HDR_OFFSET_CRC 16
#define HDR_OFFSET_ENTRIES_LBA 72
#define HDR_OFFSET_ENTRY_COUNT 80
#define HDR_OFFSET_ENTRY_SIZE 84
#define HDR_OFFSET_ENTRIES_CRC 88
#define HDR_MIN_SIZE 92

/* Entry layout (table 5.6). */
#define ENT_OFFSET_ID 16
#define ENT_OFFSET_START 32
#define ENT_OFFSET_END 40
#define ENT_OFFSET_ATTR 48
#define ENT_OFFSET_NAME 56
#define ENT_MIN_SIZE 128

#define GPT_MAX_ENTRY_BYTES (128 * 128)
#define GPT_MAX_BLOCK 4096

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int gpt_guid_parse(const char *text, gpt_guid_t *guid)
{
    /* Text order of the 16 bytes' positions in the on-disk layout. */
    static const int order[16] = {3, 2, 1, 0, 5, 4, 7, 6,
                                  8, 9, 10, 11, 12, 13, 14, 15};
    int n = 0;

    if (text == NULL || guid == NULL)
        return -1;

    for (int i = 0; i < 36; i++)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (text[i] != '-')
                return -1;
            continue;
        }

        int hi = hex_digit(text[i]);
        int lo = hex_digit(text[i + 1]);

        if (hi < 0 || lo < 0)
            return -1;

        guid->b[order[n++]] = (uint8_t)(hi << 4 | lo);
        i++;
    }

    return text[36] == '\0' ? 0 : -1;
}

int gpt_guid_equal(const gpt_guid_t *a, const gpt_guid_t *b)
{
    return k_memcmp(a->b, b->b, 16) == 0;
}

int gpt_guid_is_zero(const gpt_guid_t *guid)
{
    for (int i = 0; i < 16; i++)
    {
        if (guid->b[i] != 0)
            return 0;
    }

    return 1;
}

int gpt_read(block_device_t *device, gpt_partition_t *partitions, int max)
{
    uint8_t block[GPT_MAX_BLOCK];
    static uint8_t entries[GPT_MAX_ENTRY_BYTES + GPT_MAX_BLOCK];

    if (device == NULL || partitions == NULL || max <= 0 ||
        device->block_size < 512 || device->block_size > GPT_MAX_BLOCK ||
        device->block_count < 3)
        return -1;

    if (block_read(device, 1, 1, block) != 0)
        return -1;

    if (le32(block) != GPT_SIGNATURE_LO || le32(block + 4) != GPT_SIGNATURE_HI)
        return 0;   /* no GPT */

    uint32_t size = le32(block + HDR_OFFSET_SIZE);

    if (size < HDR_MIN_SIZE || size > device->block_size)
        return -1;

    /* The header CRC is computed with its own CRC field zeroed. */
    uint32_t want = le32(block + HDR_OFFSET_CRC);

    k_memset(block + HDR_OFFSET_CRC, 0, 4);

    if (k_crc32(0, block, size) != want)
        return -1;

    uint64_t entries_lba = le64(block + HDR_OFFSET_ENTRIES_LBA);
    uint32_t count = le32(block + HDR_OFFSET_ENTRY_COUNT);
    uint32_t entry_size = le32(block + HDR_OFFSET_ENTRY_SIZE);
    uint32_t entries_crc = le32(block + HDR_OFFSET_ENTRIES_CRC);

    if (entry_size < ENT_MIN_SIZE || (entry_size & (entry_size - 1)) != 0 ||
        entry_size > device->block_size || count == 0 || count > 128)
        return -1;

    uint32_t bytes = count * entry_size;
    uint32_t blocks = (bytes + device->block_size - 1) / device->block_size;

    if (entries_lba < 2 || entries_lba + blocks > device->block_count)
        return -1;

    if (block_read(device, entries_lba, blocks, entries) != 0)
        return -1;

    if (k_crc32(0, entries, bytes) != entries_crc)
        return -1;

    int used = 0;

    for (uint32_t i = 0; i < count && used < max; i++)
    {
        const uint8_t *e = entries + (size_t)i * entry_size;
        gpt_partition_t *p = &partitions[used];

        k_memcpy(p->type.b, e, 16);

        if (gpt_guid_is_zero(&p->type))
            continue;

        k_memcpy(p->id.b, e + ENT_OFFSET_ID, 16);

        uint64_t first = le64(e + ENT_OFFSET_START);
        uint64_t last = le64(e + ENT_OFFSET_END);

        if (last < first || last >= device->block_count)
            return -1;

        p->start = first;
        p->block_count = last - first + 1;
        p->attributes = le64(e + ENT_OFFSET_ATTR);

        int n = 0;

        for (int c = 0; c < GPT_NAME_SIZE - 1; c++)
        {
            uint16_t u = (uint16_t)(e[ENT_OFFSET_NAME + c * 2] |
                                    e[ENT_OFFSET_NAME + c * 2 + 1] << 8);

            if (u == 0)
                break;

            p->name[n++] = u < 0x80 ? (char)u : '?';
        }

        p->name[n] = '\0';
        used++;
    }

    return used;
}

int gpt_find_by_name(block_device_t *device, const char *name,
                     gpt_partition_t *partition)
{
    gpt_partition_t parts[GPT_MAX_PARTITIONS];

    if (name == NULL || partition == NULL)
        return -1;

    int count = gpt_read(device, parts, GPT_MAX_PARTITIONS);

    for (int i = 0; i < count; i++)
    {
        if (k_strcmp(parts[i].name, name) == 0)
        {
            *partition = parts[i];
            return 0;
        }
    }

    return -1;
}

int gpt_find_by_type(block_device_t *device, const char *type_text,
                     gpt_partition_t *partition)
{
    gpt_partition_t parts[GPT_MAX_PARTITIONS];
    gpt_guid_t type;

    if (partition == NULL || gpt_guid_parse(type_text, &type) != 0)
        return -1;

    int count = gpt_read(device, parts, GPT_MAX_PARTITIONS);

    for (int i = 0; i < count; i++)
    {
        if (gpt_guid_equal(&parts[i].type, &type))
        {
            *partition = parts[i];
            return 0;
        }
    }

    return -1;
}

static int view_read(block_device_t *device, uint64_t lba, uint32_t count,
                     void *buffer)
{
    gpt_view_t *view = device->driver_data;

    return block_read(view->parent, view->start + lba, count, buffer);
}

static int view_write(block_device_t *device, uint64_t lba, uint32_t count,
                      const void *buffer)
{
    gpt_view_t *view = device->driver_data;

    return block_write(view->parent, view->start + lba, count, buffer);
}

static const block_ops_t view_ops = {view_read, view_write};

int gpt_view_register(gpt_view_t *view, const char *name,
                      block_device_t *parent, uint64_t start,
                      uint64_t block_count)
{
    if (view == NULL || name == NULL || parent == NULL || block_count == 0 ||
        start >= parent->block_count ||
        block_count > parent->block_count - start)
        return -1;

    view->parent = parent;
    view->start = start;
    view->device.name = name;
    view->device.block_size = parent->block_size;
    view->device.block_count = block_count;
    view->device.ops = &view_ops;
    view->device.driver_data = view;

    return block_register(&view->device);
}
