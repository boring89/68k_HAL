/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MAC_RSRC_H
#define KERNEL_MAC_RSRC_H

#include <stdint.h>

/*
 * Finding one resource in a Macintosh resource fork, read through a
 * callback (the fork may be on a disk). Format: Inside Macintosh: More
 * Macintosh Toolbox, "Resource Manager", "Resource File Format": a 16-byte
 * header (offset to the data, offset to the map, their lengths), the data
 * (each resource is a 4-byte length and its bytes) and the map (type list,
 * reference lists, names). All numbers are big-endian.
 */

/* Read length bytes at offset of the fork into buffer. Returns 0, or -1. */
typedef int (*mac_rsrc_read_fn)(void *context, uint32_t offset, void *buffer,
                                uint32_t length);

/* A four-character code such as 'vm  ', from its four letters. */
#define MAC_RSRC_TYPE(a, b, c, d)                                              \
    (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) |         \
     ((uint32_t)(uint8_t)(c) << 8) | (uint32_t)(uint8_t)(d))

/*
 * Find resource (type, id) in a fork of fork_length bytes. On success
 * *data_offset is where its bytes start in the fork (after the length word)
 * and *data_length how many there are. Returns 0, 1 if the fork has no such
 * resource (or no resources at all), or -1 if reading fails or the fork is
 * damaged (offsets outside the fork).
 */
int mac_rsrc_find(mac_rsrc_read_fn read, void *context, uint32_t fork_length,
                  uint32_t type, int16_t id, uint32_t *data_offset,
                  uint32_t *data_length);

#endif
