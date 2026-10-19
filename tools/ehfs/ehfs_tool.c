/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ehfs_tool.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "kernel/memory/heap.h"

/* kernel/block/cache.c allocates with the kernel's heap; here that is the C library's. */
void *kmalloc(size_t size)
{
    return malloc(size);
}

int kfree(void *ptr)
{
    free(ptr);
    return 0;
}

static int file_read(block_device_t *d, uint64_t lba, uint32_t count, void *buffer)
{
    tool_disk_t *disk = d->driver_data;
    size_t want = (size_t)count * 512;
    ssize_t n = pread(disk->fd, buffer, want, (off_t)(lba * 512));

    return n == (ssize_t)want ? 0 : -1;
}

static int file_write(block_device_t *d, uint64_t lba, uint32_t count, const void *buffer)
{
    tool_disk_t *disk = d->driver_data;
    size_t want = (size_t)count * 512;
    ssize_t n = pwrite(disk->fd, buffer, want, (off_t)(lba * 512));

    return n == (ssize_t)want ? 0 : -1;
}

static const block_ops_t read_ops = {file_read, NULL};
static const block_ops_t write_ops = {file_read, file_write};

int tool_open(tool_disk_t *disk, const char *path, bool writable, bool allow_device, const char *program)
{
    struct stat st;

    memset(disk, 0, sizeof *disk);
    if (stat(path, &st) != 0)
    {
        fprintf(stderr, "%s: %s: %s\n", program, path, strerror(errno));
        return -1;
    }
    if (!S_ISREG(st.st_mode) && !(allow_device && S_ISBLK(st.st_mode)))
    {
        fprintf(stderr, "%s: %s is not a regular file (an image); a disk needs --device\n", program, path);
        return -1;
    }
    disk->fd = open(path, writable ? O_RDWR : O_RDONLY);
    if (disk->fd < 0)
    {
        fprintf(stderr, "%s: %s: %s\n", program, path, strerror(errno));
        return -1;
    }

    off_t size = lseek(disk->fd, 0, SEEK_END);

    if (size < 0)
    {
        fprintf(stderr, "%s: %s: cannot tell its size\n", program, path);
        close(disk->fd);
        return -1;
    }
    disk->dev.name = "ehfs";
    disk->dev.block_size = 512;
    disk->dev.block_count = (uint64_t)size / 512;
    disk->dev.ops = writable ? &write_ops : &read_ops;
    disk->dev.driver_data = disk;
    return 0;
}

void tool_close(tool_disk_t *disk)
{
    if (disk->fd >= 0)
    {
        fsync(disk->fd);
        close(disk->fd);
    }
    disk->fd = -1;
}

const char *tool_error(int code)
{
    switch (code)
    {
    case EHFS_OK:
        return "no error";
    case EHFS_EIO:
        return "input/output error";
    case EHFS_ECORRUPT:
        return "the volume is damaged";
    case EHFS_ENOSPC:
        return "no space left";
    case EHFS_EINVAL:
        return "invalid argument";
    case EHFS_EROFS:
        return "read-only";
    case EHFS_ETXN:
        return "the change is too large for one transaction";
    case EHFS_ENOENT:
        return "no such file or folder";
    case EHFS_EEXIST:
        return "it exists already";
    case EHFS_ENOTDIR:
        return "not a folder";
    case EHFS_EISDIR:
        return "a folder";
    case EHFS_ENOTEMPTY:
        return "the folder is not empty";
    case EHFS_ENAME:
        return "not a valid name";
    case EHFS_EUNSUPPORTED:
        return "a feature this program does not know";
    default:
        return "unknown error";
    }
}

uint64_t tool_now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
