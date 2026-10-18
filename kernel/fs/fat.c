/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "fat.h"

#include "kernel/lib/k_memory.h"

#define SECTOR 512

/* BPB fields (offsets in the boot sector). */
#define BPB_BYTES_PER_SECTOR 11
#define BPB_SECTORS_PER_CLUSTER 13
#define BPB_RESERVED_SECTORS 14
#define BPB_FAT_COUNT 16
#define BPB_ROOT_ENTRIES 17
#define BPB_FAT_SIZE_16 22
#define BPB_FAT_SIZE_32 36
#define BPB_ROOT_CLUSTER 44

/* Directory entries. */
#define DIR_ENTRY_SIZE 32
#define DIR_ATTR 11
#define DIR_CLUSTER_HIGH 20
#define DIR_CLUSTER_LOW 26
#define DIR_SIZE 28
#define ATTR_DIRECTORY 0x10
#define ATTR_VOLUME_ID 0x08
#define ATTR_LONG_NAME 0x0F
#define ENTRY_FREE 0xE5

#define FAT32_END 0x0FFFFFF8u
#define FAT32_MASK 0x0FFFFFFFu
#define MAX_CHAIN 65536     /* clusters walked in one directory: a loop guard */

typedef struct
{
    block_device_t *device;
    uint32_t sectors_per_cluster;
    uint32_t fat_start;
    uint32_t data_start;
    uint32_t cluster_count;
    uint32_t root_cluster;
} fat_volume_t;

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int open_volume(block_device_t *device, fat_volume_t *v)
{
    uint8_t boot[SECTOR];

    if (device == NULL || device->block_size != SECTOR || block_read(device, 0, 1, boot) != 0)
        return -1;

    if (boot[510] != 0x55 || boot[511] != 0xAA || le16(boot + BPB_BYTES_PER_SECTOR) != SECTOR)
        return -1;

    uint32_t per_cluster = boot[BPB_SECTORS_PER_CLUSTER];
    uint32_t reserved = le16(boot + BPB_RESERVED_SECTORS);
    uint32_t fats = boot[BPB_FAT_COUNT];
    uint32_t fat_size = le32(boot + BPB_FAT_SIZE_32);

    /* FAT32: no fixed root directory, FAT size in the 32-bit field. */
    if (per_cluster == 0 || (per_cluster & (per_cluster - 1)) != 0 || reserved == 0 || fats == 0 ||
        le16(boot + BPB_ROOT_ENTRIES) != 0 || le16(boot + BPB_FAT_SIZE_16) != 0 || fat_size == 0)
        return -1;

    v->device = device;
    v->sectors_per_cluster = per_cluster;
    v->fat_start = reserved;
    v->data_start = reserved + fats * fat_size;
    v->root_cluster = le32(boot + BPB_ROOT_CLUSTER);

    if (v->data_start >= device->block_count)
        return -1;

    v->cluster_count = (uint32_t)((device->block_count - v->data_start) / per_cluster);

    return v->root_cluster >= 2 && v->root_cluster < v->cluster_count + 2 ? 0 : -1;
}

/* The FAT entry of cluster (the next one in its chain); 0 on an I/O error. */
static uint32_t next_cluster(const fat_volume_t *v, uint32_t cluster)
{
    uint8_t sector[SECTOR];
    uint32_t offset = cluster * 4;

    if (block_read(v->device, v->fat_start + offset / SECTOR, 1, sector) != 0)
        return 0;

    return le32(sector + offset % SECTOR) & FAT32_MASK;
}

static uint64_t cluster_sector(const fat_volume_t *v, uint32_t cluster)
{
    return v->data_start + (uint64_t)(cluster - 2) * v->sectors_per_cluster;
}

static int valid_cluster(const fat_volume_t *v, uint32_t cluster)
{
    return cluster >= 2 && cluster < v->cluster_count + 2;
}

/* "grubenv" -> "GRUBENV    ": the 11 bytes of an 8.3 directory name; -1 if part is not 8.3. */
static int short_name(const char *part, int length, uint8_t name[11])
{
    int base = 0;
    int ext = 0;
    int in_ext = 0;

    k_memset(name, ' ', 11);

    for (int i = 0; i < length; i++)
    {
        char c = part[i];

        if (c == '.')
        {
            if (in_ext || base == 0)
                return -1;
            in_ext = 1;
            continue;
        }

        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');

        if (c <= ' ' || c == '/' || c == '\\')
            return -1;

        if (in_ext)
        {
            if (ext == 3)
                return -1;
            name[8 + ext++] = (uint8_t)c;
        }
        else
        {
            if (base == 8)
                return -1;
            name[base++] = (uint8_t)c;
        }
    }

    return base > 0 ? 0 : -1;
}

/* Look for name in the directory starting at cluster; fills the entry (32 bytes). */
static int find_entry(const fat_volume_t *v, uint32_t cluster, const uint8_t name[11], uint8_t entry[32])
{
    uint8_t sector[SECTOR];

    for (uint32_t walked = 0; valid_cluster(v, cluster) && walked < MAX_CHAIN; walked++)
    {
        for (uint32_t s = 0; s < v->sectors_per_cluster; s++)
        {
            if (block_read(v->device, cluster_sector(v, cluster) + s, 1, sector) != 0)
                return -1;

            for (int e = 0; e < SECTOR; e += DIR_ENTRY_SIZE)
            {
                const uint8_t *d = sector + e;

                if (d[0] == 0)
                    return -1;      /* the end of the directory */

                if (d[0] == ENTRY_FREE || (d[DIR_ATTR] & ATTR_LONG_NAME) == ATTR_LONG_NAME ||
                    (d[DIR_ATTR] & ATTR_VOLUME_ID))
                    continue;

                if (k_memcmp(d, name, 11) == 0)
                {
                    k_memcpy(entry, d, 32);
                    return 0;
                }
            }
        }

        cluster = next_cluster(v, cluster);
    }

    return -1;
}

int fat_find(block_device_t *device, const char *path, fat_file_t *file)
{
    fat_volume_t v;
    uint8_t entry[32];

    if (path == NULL || file == NULL || open_volume(device, &v) != 0)
        return -1;

    uint32_t cluster = v.root_cluster;
    const char *p = path;

    for (;;)
    {
        const char *end = p;

        while (*end != '\0' && *end != '/')
            end++;

        uint8_t name[11];

        if (short_name(p, (int)(end - p), name) != 0 || find_entry(&v, cluster, name, entry) != 0)
            return -1;

        cluster = (uint32_t)le16(entry + DIR_CLUSTER_HIGH) << 16 | le16(entry + DIR_CLUSTER_LOW);

        if (*end == '\0')
            break;

        if (!(entry[DIR_ATTR] & ATTR_DIRECTORY))
            return -1;

        p = end + 1;
    }

    if (entry[DIR_ATTR] & ATTR_DIRECTORY)
        return -1;

    file->size = le32(entry + DIR_SIZE);
    file->sector_count = (file->size + SECTOR - 1) / SECTOR;

    if (file->sector_count > FAT_FILE_MAX_SECTORS)
        return -1;

    /* The sectors, cluster by cluster along the chain. */
    for (uint32_t n = 0; n < file->sector_count;)
    {
        if (!valid_cluster(&v, cluster))
            return -1;

        for (uint32_t s = 0; s < v.sectors_per_cluster && n < file->sector_count; s++)
            file->sector[n++] = cluster_sector(&v, cluster) + s;

        if (n < file->sector_count)
        {
            cluster = next_cluster(&v, cluster);

            if (cluster >= FAT32_END)
                return -1;
        }
    }

    return 0;
}

int fat_read(block_device_t *device, const fat_file_t *file, void *buffer, uint32_t buffer_size)
{
    uint8_t sector[SECTOR];
    uint8_t *out = buffer;

    if (device == NULL || file == NULL || buffer == NULL || file->size > buffer_size)
        return -1;

    for (uint32_t i = 0; i < file->sector_count; i++)
    {
        uint32_t n = file->size - i * SECTOR < SECTOR ? file->size - i * SECTOR : SECTOR;

        if (block_read(device, file->sector[i], 1, sector) != 0)
            return -1;

        k_memcpy(out + i * SECTOR, sector, n);
    }

    return (int)file->size;
}

int fat_overwrite(block_device_t *device, const fat_file_t *file, const void *data)
{
    uint8_t sector[SECTOR];
    const uint8_t *in = data;

    if (device == NULL || file == NULL || data == NULL)
        return -1;

    for (uint32_t i = 0; i < file->sector_count; i++)
    {
        uint32_t n = file->size - i * SECTOR < SECTOR ? file->size - i * SECTOR : SECTOR;

        if (n < SECTOR && block_read(device, file->sector[i], 1, sector) != 0)
            return -1;

        k_memcpy(sector, in + i * SECTOR, n);

        if (block_write(device, file->sector[i], 1, sector) != 0)
            return -1;
    }

    return 0;
}
