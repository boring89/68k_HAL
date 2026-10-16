/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ramfs.h"

#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/memory/heap.h"

/* One file or directory. Children of a directory form a singly linked list. */
typedef struct node
{
    char name[VFS_NAME_MAX + 1];
    int is_dir;
    uint32_t inode;
    struct node *parent;
    struct node *first_child; /* directories */
    struct node *next;        /* next sibling */
    uint8_t *data;            /* files */
    uint32_t size;
    uint32_t capacity;
    int open_count;
    int unlinked; /* removed while still open: freed by the last close */
} node_t;

typedef struct
{
    node_t *root;
    uint32_t next_inode;
} ramfs_t;

/* A file can never get bigger than this: a runaway program must not eat the heap. */
#define RAMFS_MAX_FILE (16u * 1024 * 1024)

static node_t *node_new(ramfs_t *ramfs, const char *name, int is_dir, node_t *parent)
{
    node_t *node = kmalloc(sizeof(*node));

    if (node == NULL)
        return NULL;

    k_memset(node, 0, sizeof(*node));
    k_strcpy(node->name, name);
    node->is_dir = is_dir;
    node->inode = ramfs->next_inode++;
    node->parent = parent;

    return node;
}

static void node_unlink_from_parent(node_t *node)
{
    node_t **link = &node->parent->first_child;

    while (*link != node)
        link = &(*link)->next;

    *link = node->next;
}

static void node_free(node_t *node)
{
    if (node->data != NULL)
        kfree(node->data);

    kfree(node);
}

static node_t *find_child(node_t *dir, const char *name, size_t length)
{
    for (node_t *child = dir->first_child; child != NULL; child = child->next)
    {
        if (k_strlen(child->name) == length && k_strncmp(child->name, name, length) == 0)
            return child;
    }

    return NULL;
}

/*
 * Walk path from the root. Returns the node, or NULL with *missing_parent
 * set to the directory that would hold the last component (when only
 * that component is missing) and *last to its name.
 */
static node_t *lookup(ramfs_t *ramfs, const char *path, node_t **parent_out,
                      const char **last_out, int32_t *error)
{
    node_t *node = ramfs->root;

    *parent_out = NULL;
    *last_out = NULL;
    *error = 0;

    while (*path != '\0')
    {
        const char *end = path;

        while (*end != '\0' && *end != '/')
            end++;

        if (!node->is_dir)
        {
            *error = -K_ENOTDIR;
            return NULL;
        }

        node_t *child = find_child(node, path, (size_t)(end - path));

        if (child == NULL)
        {
            if (*end == '\0')
            {
                *parent_out = node;
                *last_out = path;
                *error = -K_ENOENT;
            }
            else
            {
                *error = -K_ENOENT;
            }

            return NULL;
        }

        node = child;
        path = *end == '/' ? end + 1 : end;
    }

    return node;
}

static int32_t file_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    node_t *node = file->priv;

    if (node->is_dir)
        return -K_EISDIR;

    if (pos >= node->size)
        return 0;

    if (count > node->size - pos)
        count = node->size - (uint32_t)pos;

    k_memcpy(buffer, node->data + pos, count);

    return (int32_t)count;
}

static int32_t grow(node_t *node, uint32_t needed)
{
    if (needed <= node->capacity)
        return 0;

    if (needed > RAMFS_MAX_FILE)
        return -K_EFBIG;

    uint32_t capacity = node->capacity < 256 ? 256 : node->capacity;

    while (capacity < needed)
        capacity *= 2;

    uint8_t *data = kmalloc(capacity);

    if (data == NULL)
        return -K_ENOMEM;

    if (node->data != NULL)
    {
        k_memcpy(data, node->data, node->size);
        kfree(node->data);
    }

    node->data = data;
    node->capacity = capacity;

    return 0;
}

static int32_t file_write(vfs_file_t *file, uint64_t pos, const void *buffer, uint32_t count)
{
    node_t *node = file->priv;

    if (node->is_dir)
        return -K_EISDIR;

    if (file->flags & VFS_O_APPEND)
        pos = node->size;

    if (pos > RAMFS_MAX_FILE || count > RAMFS_MAX_FILE - pos)
        return -K_EFBIG;

    uint32_t end = (uint32_t)pos + count;
    int32_t status = grow(node, end);

    if (status != 0)
        return status;

    /* Writing past the end leaves a hole that reads as zeros. */
    if (pos > node->size)
        k_memset(node->data + node->size, 0, (uint32_t)pos - node->size);

    k_memcpy(node->data + pos, buffer, count);

    if (end > node->size)
        node->size = end;

    if (file->flags & VFS_O_APPEND)
        file->pos = node->size - count; /* vfs_write adds count */

    return (int32_t)count;
}

static int32_t file_truncate(vfs_file_t *file, uint64_t size)
{
    node_t *node = file->priv;

    if (node->is_dir)
        return -K_EISDIR;

    if (size > RAMFS_MAX_FILE)
        return -K_EFBIG;

    int32_t status = grow(node, (uint32_t)size);

    if (status != 0)
        return status;

    if (size > node->size)
        k_memset(node->data + node->size, 0, (uint32_t)size - node->size);

    node->size = (uint32_t)size;

    return 0;
}

static void fill_stat(const node_t *node, vfs_stat_t *stat)
{
    stat->mode = node->is_dir ? (VFS_S_IFDIR | 0755) : (VFS_S_IFREG | 0644);
    stat->size = node->is_dir ? 0 : node->size;
    stat->inode = node->inode;
    stat->mtime = 0;
}

static int32_t file_stat(vfs_file_t *file, vfs_stat_t *stat)
{
    fill_stat(file->priv, stat);

    return 0;
}

static int32_t file_readdir(vfs_file_t *file, uint32_t index, vfs_dirent_t *entry)
{
    node_t *dir = file->priv;

    if (!dir->is_dir)
        return -K_ENOTDIR;

    /* Entries 0 and 1 are "." and "..", like on any Unix. */
    if (index < 2)
    {
        k_strcpy(entry->name, index == 0 ? "." : "..");
        entry->inode = dir->inode;
        entry->type = VFS_DT_DIR;

        return 0;
    }

    node_t *child = dir->first_child;

    for (uint32_t i = 2; child != NULL && i < index; i++)
        child = child->next;

    if (child == NULL)
        return 1;

    k_strcpy(entry->name, child->name);
    entry->inode = child->inode;
    entry->type = child->is_dir ? VFS_DT_DIR : VFS_DT_REG;

    return 0;
}

static void file_close(vfs_file_t *file)
{
    node_t *node = file->priv;

    node->open_count--;

    /* An unlinked file lives on until the last close. */
    if (node->unlinked && node->open_count == 0)
        node_free(node);
}

static const vfs_file_ops_t file_ops = {
    .read = file_read,
    .write = file_write,
    .readdir = file_readdir,
    .stat = file_stat,
    .truncate = file_truncate,
    .close = file_close,
};

static int32_t ramfs_open(vfs_fs_t *fs, const char *path, uint32_t flags,
                          uint32_t mode, vfs_file_t **file)
{
    ramfs_t *ramfs = fs->priv;
    node_t *parent;
    const char *last;
    int32_t error;
    node_t *node = lookup(ramfs, path, &parent, &last, &error);

    (void)mode;

    if (node == NULL)
    {
        if (parent == NULL || !(flags & VFS_O_CREAT))
            return error;

        node = node_new(ramfs, last, 0, parent);

        if (node == NULL)
            return -K_ENOMEM;

        node->next = parent->first_child;
        parent->first_child = node;
    }
    else if (flags & VFS_O_CREAT && flags & VFS_O_EXCL)
    {
        return -K_EEXIST;
    }

    if (node->is_dir && (flags & VFS_O_ACCMODE) != VFS_O_RDONLY)
        return -K_EISDIR;

    if (!node->is_dir && (flags & VFS_O_DIRECTORY))
        return -K_ENOTDIR;

    if (!node->is_dir && (flags & VFS_O_TRUNC) && (flags & VFS_O_ACCMODE) != VFS_O_RDONLY)
        node->size = 0;

    *file = vfs_file_new(&file_ops, node, flags);

    if (*file == NULL)
        return -K_ENOMEM;

    node->open_count++;

    return 0;
}

static int32_t ramfs_stat(vfs_fs_t *fs, const char *path, vfs_stat_t *stat)
{
    node_t *parent;
    const char *last;
    int32_t error;
    node_t *node = lookup(fs->priv, path, &parent, &last, &error);

    if (node == NULL)
        return error;

    fill_stat(node, stat);

    return 0;
}

static int32_t ramfs_mkdir(vfs_fs_t *fs, const char *path, uint32_t mode)
{
    ramfs_t *ramfs = fs->priv;
    node_t *parent;
    const char *last;
    int32_t error;
    node_t *node = lookup(ramfs, path, &parent, &last, &error);

    (void)mode;

    if (node != NULL)
        return -K_EEXIST;

    if (parent == NULL)
        return error;

    node = node_new(ramfs, last, 1, parent);

    if (node == NULL)
        return -K_ENOMEM;

    node->next = parent->first_child;
    parent->first_child = node;

    return 0;
}

static int32_t remove_node(vfs_fs_t *fs, const char *path, int want_dir)
{
    node_t *parent;
    const char *last;
    int32_t error;
    node_t *node = lookup(fs->priv, path, &parent, &last, &error);

    if (node == NULL)
        return error;

    if (node->parent == NULL)
        return -K_EPERM; /* the root */

    if (want_dir && !node->is_dir)
        return -K_ENOTDIR;

    if (!want_dir && node->is_dir)
        return -K_EISDIR;

    if (node->is_dir && node->first_child != NULL)
        return -K_ENOTEMPTY;

    node_unlink_from_parent(node);

    if (node->open_count > 0)
        node->unlinked = 1;
    else
        node_free(node);

    return 0;
}

static int32_t ramfs_unlink(vfs_fs_t *fs, const char *path)
{
    return remove_node(fs, path, 0);
}

static int32_t ramfs_rmdir(vfs_fs_t *fs, const char *path)
{
    return remove_node(fs, path, 1);
}

static int32_t ramfs_rename(vfs_fs_t *fs, const char *from, const char *to)
{
    node_t *parent;
    const char *last;
    int32_t error;
    node_t *node = lookup(fs->priv, from, &parent, &last, &error);

    if (node == NULL)
        return error;

    if (node->parent == NULL)
        return -K_EPERM;

    node_t *target_parent;
    const char *target_last;
    node_t *target = lookup(fs->priv, to, &target_parent, &target_last, &error);

    if (target == node)
        return 0;

    if (target != NULL)
    {
        /* Replace a file by a file, an empty directory by a directory. */
        if (target->is_dir != node->is_dir)
            return target->is_dir ? -K_EISDIR : -K_ENOTDIR;

        error = remove_node(fs, to, target->is_dir);

        if (error != 0)
            return error;

        target = lookup(fs->priv, to, &target_parent, &target_last, &error);
    }

    if (target_parent == NULL)
        return error;

    /* A directory must not move into itself. */
    for (node_t *up = target_parent; up != NULL; up = up->parent)
    {
        if (up == node)
            return -K_EINVAL;
    }

    node_unlink_from_parent(node);
    k_strcpy(node->name, target_last);
    node->parent = target_parent;
    node->next = target_parent->first_child;
    target_parent->first_child = node;

    return 0;
}

vfs_fs_t *ramfs_create(void)
{
    vfs_fs_t *fs = kmalloc(sizeof(*fs));
    ramfs_t *ramfs = kmalloc(sizeof(*ramfs));

    if (fs == NULL || ramfs == NULL)
    {
        if (fs != NULL)
            kfree(fs);

        if (ramfs != NULL)
            kfree(ramfs);

        return NULL;
    }

    ramfs->next_inode = 1;
    ramfs->root = node_new(ramfs, "", 1, NULL);

    if (ramfs->root == NULL)
    {
        kfree(fs);
        kfree(ramfs);

        return NULL;
    }

    fs->open = ramfs_open;
    fs->stat = ramfs_stat;
    fs->mkdir = ramfs_mkdir;
    fs->unlink = ramfs_unlink;
    fs->rmdir = ramfs_rmdir;
    fs->rename = ramfs_rename;
    fs->priv = ramfs;

    return fs;
}

static void destroy_tree(node_t *node)
{
    while (node->first_child != NULL)
    {
        node_t *child = node->first_child;

        node->first_child = child->next;
        destroy_tree(child);
    }

    node_free(node);
}

void ramfs_destroy(vfs_fs_t *fs)
{
    ramfs_t *ramfs = fs->priv;

    destroy_tree(ramfs->root);
    kfree(ramfs);
    kfree(fs);
}
