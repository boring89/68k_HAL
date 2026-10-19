/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * mkfs.ehfs: make an empty eHFS volume (docs/ehfs.md).
 *
 * Usage: mkfs.ehfs [-L name] [--offset SECTORS] [--sectors N] [--journal BLOCKS] [--device] TARGET
 *
 *   TARGET       an image file (a disk only with --device)
 *   -L           the volume's name (UTF-8; default "Data")
 *   --offset     where the volume starts, in 512-byte sectors (a partition's first sector)
 *   --sectors    how many sectors it has (default: to the end of TARGET)
 *   --journal    journal blocks (default: 1/256 of the volume, 260..16384)
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ehfs_tool.h"

static ehfs_t fs;

int main(int argc, char **argv)
{
    const char *target = NULL;
    ehfs_format_options_t options;
    uint64_t offset = 0, sectors = 0;
    bool device = false;

    memset(&options, 0, sizeof options);
    options.name = "Data";
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-L") == 0 && i + 1 < argc)
            options.name = argv[++i];
        else if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc)
            offset = strtoull(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--sectors") == 0 && i + 1 < argc)
            sectors = strtoull(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--journal") == 0 && i + 1 < argc)
            options.journal_blocks = strtoull(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--device") == 0)
            device = true;
        else if (argv[i][0] != '-' && target == NULL)
            target = argv[i];
        else
        {
            fprintf(stderr, "usage: mkfs.ehfs [-L name] [--offset SECTORS] [--sectors N] [--journal BLOCKS] "
                            "[--device] TARGET\n");
            return 2;
        }
    }
    if (target == NULL)
    {
        fprintf(stderr, "mkfs.ehfs: no target\n");
        return 2;
    }

    tool_disk_t disk;

    if (tool_open(&disk, target, true, device, "mkfs.ehfs") != 0)
        return 1;
    if (offset >= disk.dev.block_count)
    {
        fprintf(stderr, "mkfs.ehfs: the offset is past the end of %s\n", target);
        return 1;
    }
    if (sectors == 0)
        sectors = disk.dev.block_count - offset;

    int urandom = open("/dev/urandom", O_RDONLY);

    if (urandom < 0 || read(urandom, options.uuid, 16) != 16)
    {
        fprintf(stderr, "mkfs.ehfs: no random numbers for the UUID\n");
        return 1;
    }
    close(urandom);
    options.now = tool_now();

    int r = ehfs_format(&fs, &disk.dev, offset, sectors, &options);

    tool_close(&disk);
    if (r != EHFS_OK)
    {
        fprintf(stderr, "mkfs.ehfs: %s: %s\n", target, tool_error(r));
        return 1;
    }
    printf("mkfs.ehfs: %s: \"%s\", %llu blocks of 4 KiB\n", target, options.name,
           (unsigned long long)(sectors / EHFS_SECTORS_PER_BLOCK));
    return 0;
}
