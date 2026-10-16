/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tarfs.h"

#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/memory/heap.h"

/*
 * ustar header (POSIX.1-1988 pax/tar format, 512 bytes): the fields
 * used here and where they are.
 */
#define TAR_BLOCK 512
#define TAR_NAME 0       /* 100 bytes */
#define TAR_SIZE 124     /* 12 bytes, octal text */
#define TAR_TYPE 156     /* 1 byte: '0' or NUL = file, '5' = directory */
#define TAR_MAGIC 257    /* "ustar" */
#define TAR_PREFIX 345   /* 155 bytes, joined to the name with '/' */

typedef struct
{
    char *path;          /* no leading or trailing slash */
    const uint8_t *data; /* files */
    uint32_t size;
    int is_dir;
} entry_t;

typedef struct
{
    entry_t *entries;
    unsigned int count;
} tarfs_t;

static int is_zero_block(const uint8_t *block)
{
    for (int i = 0; i < TAR_BLOCK; i++)
    {
        if (block[i] != 0)
            return 0;
    }

    return 1;
}

static int parse_octal(const uint8_t *field, size_t length, uint32_t *value)
{
    uint32_t result = 0;
    size_t i = 0;

    while (i < length && field[i] == ' ')
        i++;

    for (; i < length && field[i] >= '0' && field[i] <= '7'; i++)
    {
        if (result > (0xFFFFFFFFu >> 3))
            return -1;

        result = (result << 3) | (uint32_t)(field[i] - '0');
    }

    *value = result;

    return 0;
}

/* Copy name (at most max bytes, NUL terminated or not) with slashes trimmed. */
static size_t trimmed_length(const uint8_t *name, size_t max)
{
    size_t length = 0;

    while (length < max && name[length] != '\0')
        length++;

    while (length > 0 && name[length - 1] == '/')
        length--;

    return length;
}

static const entry_t *find(const tarfs_t *tar, const char *path)
{
    for (unsigned int i = 0; i < tar->count; i++)
    {
        if (k_strcmp(tar->entries[i].path, path) == 0)
            return &tar->entries[i];
    }

    return NULL;
}

/* True if path is a directory: an entry or a parent of one. */
static int is_directory(const tarfs_t *tar, const char *path)
{
    size_t length = k_strlen(path);

    if (length == 0)
        return 1;

    for (unsigned int i = 0; i < tar->count; i++)
    {
        const entry_t *entry = &tar->entries[i];

        if (k_strcmp(entry->path, path) == 0 && entry->is_dir)
            return 1;

        if (k_strncmp(entry->path, path, length) == 0 && entry->path[length] == '/')
            return 1;
    }

    return 0;
}

typedef struct
{
    const tarfs_t *tar;
    const entry_t *entry; /* NULL for a directory */
    char *dir;            /* directory path, for listing */
} open_file_t;

static int32_t file_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    open_file_t *open = file->priv;

    if (open->entry == NULL)
        return -K_EISDIR;

    if (pos >= open->entry->size)
        return 0;

    if (count > open->entry->size - pos)
        count = open->entry->size - (uint32_t)pos;

    k_memcpy(buffer, open->entry->data + pos, count);

    return (int32_t)count;
}

static int32_t file_stat(vfs_file_t *file, vfs_stat_t *stat)
{
    open_file_t *open = file->priv;

    stat->inode = open->entry != NULL ? (uint32_t)(open->entry - open->tar->entries) + 2 : 1;
    stat->mtime = 0;

    if (open->entry == NULL)
    {
        stat->mode = VFS_S_IFDIR | 0555;
        stat->size = 0;
    }
    else
    {
        stat->mode = VFS_S_IFREG | 0555;
        stat->size = open->entry->size;
    }

    return 0;
}

/*
 * Entry number index of a directory: the distinct first components
 * below it, in archive order, after "." and "..".
 */
static int32_t file_readdir(vfs_file_t *file, uint32_t index, vfs_dirent_t *out)
{
    open_file_t *open = file->priv;

    if (open->entry != NULL)
        return -K_ENOTDIR;

    if (index < 2)
    {
        k_strcpy(out->name, index == 0 ? "." : "..");
        out->inode = 1;
        out->type = VFS_DT_DIR;

        return 0;
    }

    const tarfs_t *tar = open->tar;
    size_t dir_length = k_strlen(open->dir);
    uint32_t seen = 2;

    for (unsigned int i = 0; i < tar->count; i++)
    {
        const char *path = tar->entries[i].path;

        if (dir_length > 0)
        {
            if (k_strncmp(path, open->dir, dir_length) != 0 || path[dir_length] != '/')
                continue;

            path += dir_length + 1;
        }

        size_t part = 0;

        while (path[part] != '\0' && path[part] != '/')
            part++;

        if (part == 0 || part > VFS_NAME_MAX)
            continue;

        /* Count each name once: skip it if an earlier entry gave the same one. */
        int duplicate = 0;

        for (unsigned int j = 0; j < i && !duplicate; j++)
        {
            const char *other = tar->entries[j].path;

            if (dir_length > 0)
            {
                if (k_strncmp(other, open->dir, dir_length) != 0 || other[dir_length] != '/')
                    continue;

                other += dir_length + 1;
            }

            if (k_strncmp(other, path, part) == 0 && (other[part] == '\0' || other[part] == '/'))
                duplicate = 1;
        }

        if (duplicate)
            continue;

        if (seen++ != index)
            continue;

        k_memcpy(out->name, path, part);
        out->name[part] = '\0';
        out->inode = i + 2;
        out->type = path[part] == '/' || tar->entries[i].is_dir ? VFS_DT_DIR : VFS_DT_REG;

        return 0;
    }

    return 1;
}

static void file_close(vfs_file_t *file)
{
    open_file_t *open = file->priv;

    kfree(open->dir);
    kfree(open);
}

static const vfs_file_ops_t file_ops = {
    .read = file_read,
    .readdir = file_readdir,
    .stat = file_stat,
    .close = file_close,
};

static int32_t tarfs_open(vfs_fs_t *fs, const char *path, uint32_t flags,
                          uint32_t mode, vfs_file_t **file)
{
    const tarfs_t *tar = fs->priv;
    const entry_t *entry = find(tar, path);

    (void)mode;

    if ((flags & VFS_O_ACCMODE) != VFS_O_RDONLY || (flags & (VFS_O_CREAT | VFS_O_TRUNC)))
        return -K_EROFS;

    if (entry != NULL && entry->is_dir)
        entry = NULL;

    if (entry == NULL && !is_directory(tar, path))
        return -K_ENOENT;

    if (entry != NULL && (flags & VFS_O_DIRECTORY))
        return -K_ENOTDIR;

    open_file_t *open = kmalloc(sizeof(*open));
    char *dir = kmalloc(k_strlen(path) + 1);

    if (open == NULL || dir == NULL)
    {
        if (open != NULL)
            kfree(open);

        if (dir != NULL)
            kfree(dir);

        return -K_ENOMEM;
    }

    k_strcpy(dir, path);
    open->tar = tar;
    open->entry = entry;
    open->dir = dir;

    *file = vfs_file_new(&file_ops, open, flags);

    if (*file == NULL)
    {
        kfree(dir);
        kfree(open);

        return -K_ENOMEM;
    }

    return 0;
}

static int32_t tarfs_stat(vfs_fs_t *fs, const char *path, vfs_stat_t *stat)
{
    const tarfs_t *tar = fs->priv;
    const entry_t *entry = find(tar, path);

    stat->mtime = 0;

    if (entry != NULL && !entry->is_dir)
    {
        stat->mode = VFS_S_IFREG | 0555;
        stat->size = entry->size;
        stat->inode = (uint32_t)(entry - tar->entries) + 2;

        return 0;
    }

    if (!is_directory(tar, path))
        return -K_ENOENT;

    stat->mode = VFS_S_IFDIR | 0555;
    stat->size = 0;
    stat->inode = 1;

    return 0;
}

vfs_fs_t *tarfs_create(const void *data, size_t size)
{
    const uint8_t *archive = data;
    unsigned int capacity = 0;

    /* Pass 1: count the entries; pass 2 fills them in. */
    for (int pass = 0; pass < 2; pass++)
    {
        unsigned int count = 0;
        size_t at = 0;
        entry_t *entries = NULL;

        if (pass == 1)
        {
            entries = kmalloc((capacity ? capacity : 1) * sizeof(entry_t));

            if (entries == NULL)
                return NULL;
        }

        while (at + TAR_BLOCK <= size && !is_zero_block(archive + at))
        {
            const uint8_t *header = archive + at;
            uint32_t file_size;

            if (header[TAR_MAGIC] != 'u' || header[TAR_MAGIC + 1] != 's' ||
                header[TAR_MAGIC + 2] != 't' || header[TAR_MAGIC + 3] != 'a' ||
                header[TAR_MAGIC + 4] != 'r')
                break;

            if (parse_octal(header + TAR_SIZE, 12, &file_size) != 0)
                break;

            uint8_t type = header[TAR_TYPE];
            size_t data_at = at + TAR_BLOCK;

            if (file_size > size - data_at)
                break;

            if (type == '0' || type == 0 || type == '5')
            {
                size_t name_length = trimmed_length(header + TAR_NAME, 100);
                size_t prefix_length = trimmed_length(header + TAR_PREFIX, 155);

                /* "./name" is how GNU tar writes entries when given "." */
                const uint8_t *name = header + TAR_NAME;

                while (name_length >= 2 && name[0] == '.' && name[1] == '/')
                {
                    name += 2;
                    name_length -= 2;
                }

                if (pass == 1 && name_length > 0 && !(name_length == 1 && name[0] == '.'))
                {
                    char *path = kmalloc(prefix_length + 1 + name_length + 1);

                    if (path == NULL)
                        return NULL;

                    size_t n = 0;

                    if (prefix_length > 0)
                    {
                        k_memcpy(path, header + TAR_PREFIX, prefix_length);
                        n = prefix_length;
                        path[n++] = '/';
                    }

                    k_memcpy(path + n, name, name_length);
                    path[n + name_length] = '\0';

                    entries[count].path = path;
                    entries[count].data = archive + data_at;
                    entries[count].size = type == '5' ? 0 : file_size;
                    entries[count].is_dir = type == '5';
                }

                if (name_length > 0 && !(name_length == 1 && name[0] == '.'))
                    count++;
            }

            at = data_at + ((size_t)file_size + TAR_BLOCK - 1) / TAR_BLOCK * TAR_BLOCK;
        }

        if (pass == 0)
        {
            /* Not a tar archive: no entries and not just the zero end marker. */
            if (count == 0 && !(size >= TAR_BLOCK && is_zero_block(archive)))
                return NULL;

            capacity = count;
            continue;
        }

        tarfs_t *tar = kmalloc(sizeof(*tar));
        vfs_fs_t *fs = kmalloc(sizeof(*fs));

        if (tar == NULL || fs == NULL)
            return NULL;

        tar->entries = entries;
        tar->count = count;

        k_memset(fs, 0, sizeof(*fs));
        fs->open = tarfs_open;
        fs->stat = tarfs_stat;
        fs->priv = tar;

        return fs;
    }

    return NULL;
}

void tarfs_destroy(vfs_fs_t *fs)
{
    tarfs_t *tar = fs->priv;

    for (unsigned int i = 0; i < tar->count; i++)
        kfree(tar->entries[i].path);

    kfree(tar->entries);
    kfree(tar);
    kfree(fs);
}
