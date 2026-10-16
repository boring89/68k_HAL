/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "procfs.h"

#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/memory/heap.h"

typedef struct
{
    const char *name; /* points to a copy owned by the table */
    char name_copy[VFS_NAME_MAX + 1];
    procfs_generator_t generator;
} entry_t;

typedef struct
{
    entry_t entries[PROCFS_MAX_FILES];
    unsigned int count;
} procfs_t;

/* An open file: the text as it was when the file was opened, or a directory. */
typedef struct
{
    const procfs_t *procfs;
    char *text; /* NULL for the directory */
    size_t length;
} open_t;

static const entry_t *find(const procfs_t *procfs, const char *name)
{
    for (unsigned int i = 0; i < procfs->count; i++)
    {
        if (k_strcmp(procfs->entries[i].name_copy, name) == 0)
            return &procfs->entries[i];
    }

    return NULL;
}

static int32_t file_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    open_t *open = file->priv;

    if (open->text == NULL)
        return -K_EISDIR;

    if (pos >= open->length)
        return 0;

    if (count > open->length - pos)
        count = (uint32_t)(open->length - pos);

    k_memcpy(buffer, open->text + pos, count);

    return (int32_t)count;
}

static int32_t file_readdir(vfs_file_t *file, uint32_t index, vfs_dirent_t *entry)
{
    open_t *open = file->priv;

    if (open->text != NULL)
        return -K_ENOTDIR;

    if (index < 2)
    {
        k_strcpy(entry->name, index == 0 ? "." : "..");
        entry->inode = 1;
        entry->type = VFS_DT_DIR;

        return 0;
    }

    if (index - 2 >= open->procfs->count)
        return 1;

    k_strcpy(entry->name, open->procfs->entries[index - 2].name_copy);
    entry->inode = index;
    entry->type = VFS_DT_REG;

    return 0;
}

static int32_t file_stat(vfs_file_t *file, vfs_stat_t *stat)
{
    open_t *open = file->priv;

    stat->mtime = 0;

    if (open->text == NULL)
    {
        stat->mode = VFS_S_IFDIR | 0555;
        stat->size = 0;
        stat->inode = 1;

        return 0;
    }

    stat->mode = VFS_S_IFREG | 0444;
    stat->size = open->length;
    stat->inode = 2;

    return 0;
}

static void file_close(vfs_file_t *file)
{
    open_t *open = file->priv;

    kfree(open->text);
    kfree(open);
}

static const vfs_file_ops_t file_ops = {
    .read = file_read,
    .readdir = file_readdir,
    .stat = file_stat,
    .close = file_close,
};

static int32_t procfs_open(vfs_fs_t *fs, const char *path, uint32_t flags, uint32_t mode,
                           vfs_file_t **file)
{
    const procfs_t *procfs = fs->priv;
    const entry_t *entry = path[0] == '\0' ? NULL : find(procfs, path);

    (void)mode;

    if ((flags & VFS_O_ACCMODE) != VFS_O_RDONLY || (flags & (VFS_O_CREAT | VFS_O_TRUNC)))
        return -K_EROFS;

    if (path[0] != '\0' && entry == NULL)
        return -K_ENOENT;

    if (entry != NULL && (flags & VFS_O_DIRECTORY))
        return -K_ENOTDIR;

    open_t *open = kmalloc(sizeof(*open));

    if (open == NULL)
        return -K_ENOMEM;

    open->procfs = procfs;
    open->text = NULL;
    open->length = 0;

    if (entry != NULL)
    {
        open->text = kmalloc(PROCFS_MAX_SIZE);

        if (open->text == NULL)
        {
            kfree(open);

            return -K_ENOMEM;
        }

        open->length = entry->generator(open->text, PROCFS_MAX_SIZE);

        if (open->length > PROCFS_MAX_SIZE)
            open->length = PROCFS_MAX_SIZE;
    }

    *file = vfs_file_new(&file_ops, open, flags);

    if (*file == NULL)
    {
        kfree(open->text);
        kfree(open);

        return -K_ENOMEM;
    }

    (*file)->seekable = 1;

    return 0;
}

static int32_t procfs_stat(vfs_fs_t *fs, const char *path, vfs_stat_t *stat)
{
    vfs_file_t *file;
    int32_t status = procfs_open(fs, path, VFS_O_RDONLY, 0, &file);

    if (status != 0)
        return status;

    status = file_stat(file, stat);
    vfs_file_close(file);

    return status;
}

vfs_fs_t *procfs_create(void)
{
    vfs_fs_t *fs = kmalloc(sizeof(*fs));
    procfs_t *procfs = kmalloc(sizeof(*procfs));

    if (fs == NULL || procfs == NULL)
    {
        kfree(fs);
        kfree(procfs);

        return NULL;
    }

    k_memset(procfs, 0, sizeof(*procfs));
    k_memset(fs, 0, sizeof(*fs));
    fs->open = procfs_open;
    fs->stat = procfs_stat;
    fs->priv = procfs;

    return fs;
}

int procfs_add(vfs_fs_t *fs, const char *name, procfs_generator_t generator)
{
    procfs_t *procfs = fs->priv;
    size_t length = k_strlen(name);

    if (length == 0 || length > VFS_NAME_MAX || procfs->count == PROCFS_MAX_FILES || generator == NULL ||
        find(procfs, name) != NULL)
        return -1;

    for (size_t i = 0; i < length; i++)
    {
        if (name[i] == '/')
            return -1;
    }

    entry_t *entry = &procfs->entries[procfs->count++];

    k_strcpy(entry->name_copy, name);
    entry->name = entry->name_copy;
    entry->generator = generator;

    return 0;
}

void procfs_destroy(vfs_fs_t *fs)
{
    kfree(fs->priv);
    kfree(fs);
}
