/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "rsrc.h"

#include <stddef.h>

#define HEADER_SIZE 16
#define MAP_TYPE_LIST 24    /* offset in the map of the offset to the type list */
#define TYPE_ENTRY_SIZE 8   /* type, count - 1, offset to its reference list */
#define REF_ENTRY_SIZE 12   /* id, name offset, attributes + 3-byte data offset, handle */

static uint32_t be16(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8) | p[1];
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/* Read a range that must lie inside the fork. */
static int read_inside(mac_rsrc_read_fn read, void *context,
                       uint32_t fork_length, uint32_t offset, void *buffer,
                       uint32_t length)
{
    if (offset > fork_length || length > fork_length - offset)
        return -1;

    return read(context, offset, buffer, length);
}

int mac_rsrc_find(mac_rsrc_read_fn read, void *context, uint32_t fork_length,
                  uint32_t type, int16_t id, uint32_t *data_offset,
                  uint32_t *data_length)
{
    uint8_t header[HEADER_SIZE];
    uint8_t entry[REF_ENTRY_SIZE];

    if (read == NULL || data_offset == NULL || data_length == NULL)
        return -1;

    /* An empty resource fork: nothing in it. */
    if (fork_length == 0)
        return 1;

    if (read_inside(read, context, fork_length, 0, header, HEADER_SIZE) != 0)
        return -1;

    uint32_t data_start = be32(header);
    uint32_t map_start = be32(header + 4);
    uint32_t data_size = be32(header + 8);
    uint32_t map_size = be32(header + 12);

    if (data_start > fork_length || data_size > fork_length - data_start ||
        map_start > fork_length || map_size > fork_length - map_start ||
        map_size < MAP_TYPE_LIST + 4)
        return -1;

    uint8_t offsets[2];

    if (read_inside(read, context, fork_length, map_start + MAP_TYPE_LIST,
                    offsets, 2) != 0)
        return -1;

    uint32_t type_list = map_start + be16(offsets);
    uint8_t count_word[2];

    if (read_inside(read, context, fork_length, type_list, count_word, 2) != 0)
        return -1;

    /* The count is stored minus one; 0xFFFF means an empty map. */
    uint32_t types = (be16(count_word) + 1) & 0xFFFF;

    for (uint32_t t = 0; t < types; t++)
    {
        if (read_inside(read, context, fork_length,
                        type_list + 2 + t * TYPE_ENTRY_SIZE, entry,
                        TYPE_ENTRY_SIZE) != 0)
            return -1;

        if (be32(entry) != type)
            continue;

        uint32_t refs = be16(entry + 4) + 1;
        uint32_t ref_list = type_list + be16(entry + 6);

        for (uint32_t r = 0; r < refs; r++)
        {
            if (read_inside(read, context, fork_length,
                            ref_list + r * REF_ENTRY_SIZE, entry,
                            REF_ENTRY_SIZE) != 0)
                return -1;

            if ((int16_t)be16(entry) != id)
                continue;

            uint32_t start = data_start + (be32(entry + 4) & 0xFFFFFF);
            uint8_t length[4];

            if (read_inside(read, context, fork_length, start, length, 4) != 0)
                return -1;

            uint32_t size = be32(length);

            if (start + 4 > data_start + data_size ||
                size > data_start + data_size - (start + 4))
                return -1;

            *data_offset = start + 4;
            *data_length = size;

            return 0;
        }

        return 1;
    }

    return 1;
}
