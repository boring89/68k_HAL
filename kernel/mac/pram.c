/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pram.h"

#include <stdbool.h>
#include <stddef.h>

#include "kernel/fs/fat.h"
#include "kernel/lib/k_crc32.h"
#include "kernel/lib/k_memory.h"

#define MAGIC "68kHALPR"
#define MAGIC_SIZE 8
#define VERSION 1
#define OFF_VERSION 8
#define OFF_LENGTH 12
#define OFF_CRC 16
#define OFF_DATA 32

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

void mac_pram_encode(const uint8_t xpram[MAC_PRAM_SIZE], uint8_t file[MAC_PRAM_FILE_SIZE])
{
    k_memset(file, 0, MAC_PRAM_FILE_SIZE);
    k_memcpy(file, MAGIC, MAGIC_SIZE);
    put32(file + OFF_VERSION, VERSION);
    put32(file + OFF_LENGTH, MAC_PRAM_SIZE);
    put32(file + OFF_CRC, k_crc32(0, xpram, MAC_PRAM_SIZE));
    k_memcpy(file + OFF_DATA, xpram, MAC_PRAM_SIZE);
}

mac_pram_status_t mac_pram_decode(const uint8_t file[MAC_PRAM_FILE_SIZE], uint8_t xpram[MAC_PRAM_SIZE])
{
    bool empty = true;

    for (size_t i = 0; i < MAC_PRAM_FILE_SIZE; i++)
        empty = empty && file[i] == 0;

    if (empty)
        return MAC_PRAM_EMPTY;

    if (k_memcmp(file, MAGIC, MAGIC_SIZE) != 0 || get32(file + OFF_VERSION) != VERSION ||
        get32(file + OFF_LENGTH) != MAC_PRAM_SIZE ||
        get32(file + OFF_CRC) != k_crc32(0, file + OFF_DATA, MAC_PRAM_SIZE))
        return MAC_PRAM_DAMAGED;

    k_memcpy(xpram, file + OFF_DATA, MAC_PRAM_SIZE);

    return MAC_PRAM_OK;
}

static int find_file(block_device_t *boot, fat_file_t *file)
{
    if (boot == NULL || fat_find(boot, MAC_PRAM_PATH, file) != 0 || file->size != MAC_PRAM_FILE_SIZE)
        return -1;

    return 0;
}

mac_pram_status_t mac_pram_load(block_device_t *boot, uint8_t xpram[MAC_PRAM_SIZE])
{
    fat_file_t file;
    uint8_t bytes[MAC_PRAM_FILE_SIZE];

    if (find_file(boot, &file) != 0)
        return MAC_PRAM_NO_FILE;
    if (fat_read(boot, &file, bytes, sizeof bytes) != MAC_PRAM_FILE_SIZE)
        return MAC_PRAM_IO_ERROR;

    return mac_pram_decode(bytes, xpram);
}

static int write_file(block_device_t *boot, const uint8_t bytes[MAC_PRAM_FILE_SIZE])
{
    fat_file_t file;

    if (find_file(boot, &file) != 0)
        return -1;

    return fat_overwrite(boot, &file, bytes);
}

int mac_pram_save(block_device_t *boot, const uint8_t xpram[MAC_PRAM_SIZE])
{
    uint8_t bytes[MAC_PRAM_FILE_SIZE];

    mac_pram_encode(xpram, bytes);

    return write_file(boot, bytes);
}

int mac_pram_clear(block_device_t *boot)
{
    uint8_t bytes[MAC_PRAM_FILE_SIZE];

    k_memset(bytes, 0, sizeof bytes);

    return write_file(boot, bytes);
}

const char *mac_pram_status_name(mac_pram_status_t status)
{
    switch (status)
    {
    case MAC_PRAM_OK:
        return "kept";
    case MAC_PRAM_EMPTY:
        return "empty";
    case MAC_PRAM_NO_FILE:
        return "no 68k_hal/pram file";
    case MAC_PRAM_DAMAGED:
        return "damaged";
    default:
        return "read error";
    }
}
