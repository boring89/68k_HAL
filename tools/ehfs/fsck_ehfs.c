/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * fsck.ehfs: check an eHFS volume (docs/ehfs.md). It does not repair: the
 * volume is mounted read-only (a committed transaction left in the journal is
 * read through, not written home).
 *
 * Usage: fsck.ehfs [--offset SECTORS] [--device] TARGET
 *
 * Exit status: 0 clean, 1 problems found, 2 the volume cannot be read.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ehfs_tool.h"

static ehfs_t fs;

int main(int argc, char **argv)
{
    const char *target = NULL;
    uint64_t offset = 0;
    bool device = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc)
            offset = strtoull(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--device") == 0)
            device = true;
        else if (argv[i][0] != '-' && target == NULL)
            target = argv[i];
        else
        {
            fprintf(stderr, "usage: fsck.ehfs [--offset SECTORS] [--device] TARGET\n");
            return 2;
        }
    }
    if (target == NULL)
    {
        fprintf(stderr, "fsck.ehfs: no target\n");
        return 2;
    }

    tool_disk_t disk;
    ehfs_check_t report;

    if (tool_open(&disk, target, false, device, "fsck.ehfs") != 0)
        return 2;

    int r = ehfs_mount(&fs, &disk.dev, offset, true);

    if (r != EHFS_OK)
    {
        fprintf(stderr, "fsck.ehfs: %s: %s\n", target, tool_error(r));
        tool_close(&disk);
        return 2;
    }

    char name[256];
    uint32_t n = fs.sb[SB_NAME];

    memcpy(name, fs.sb + SB_NAME + 1, n);
    name[n] = '\0';
    printf("fsck.ehfs: %s: \"%s\": %llu blocks, %llu free, %llu files, %llu folders%s\n", target, name,
           (unsigned long long)fs.blocks, (unsigned long long)ehfs_sb_get64(&fs, SB_FREE_BLOCKS),
           (unsigned long long)ehfs_sb_get64(&fs, SB_FILE_COUNT),
           (unsigned long long)ehfs_sb_get64(&fs, SB_FOLDER_COUNT),
           ehfs_sb_get32(&fs, SB_STATE) == EHFS_STATE_CLEAN ? "" : " (not unmounted cleanly)");
    if (fs.txn_count > 0)
        printf("fsck.ehfs: the journal holds a committed transaction (written home at the next mount)\n");
    ehfs_check(&fs, &report);
    tool_close(&disk);
    if (report.errors != 0)
    {
        printf("fsck.ehfs: %u problem%s; the first: %s\n", report.errors, report.errors == 1 ? "" : "s",
               report.first);
        return 1;
    }
    printf("fsck.ehfs: clean\n");
    return 0;
}
