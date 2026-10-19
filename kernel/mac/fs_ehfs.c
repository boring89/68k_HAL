/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "fs_ehfs.h"

#include "emu/mac/fs_paths.h"
#include "emu/mac/mac_roman.h"
#include "kernel/fs/ehfs/catalog.h"
#include "kernel/fs/ehfs/file.h"
#include "kernel/lib/k_memory.h"

#define MAC_EPOCH_OFFSET 2082844800u    /* seconds from 1 Jan 1904 to 1 Jan 1970 */
#define NS 1000000000u

/* One entry for each step of the work: they are large (names, fork descriptors). */
static ehfs_entry_t entry, other;

static uint32_t mac_time(uint64_t ns)
{
    uint64_t seconds = ns / NS + MAC_EPOCH_OFFSET;

    return seconds > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)seconds;
}

static uint64_t ehfs_time(uint32_t mac)
{
    return mac < MAC_EPOCH_OFFSET ? 0 : (uint64_t)(mac - MAC_EPOCH_OFFSET) * NS;
}

static int os_error(int r)
{
    switch (r)
    {
    case EHFS_OK:
        return 0;
    case EHFS_ENOSPC:
        return MAC_FS_DSK_FUL_ERR;
    case EHFS_ENOENT:
        return MAC_FS_FNF_ERR;
    case EHFS_EEXIST:
        return MAC_FS_DUP_FN_ERR;
    case EHFS_ENOTDIR:
        return MAC_FS_DIR_NF_ERR;
    case EHFS_ENOTEMPTY:
        return MAC_FS_FBSY_ERR;
    case EHFS_ENAME:
        return MAC_FS_BD_NAM_ERR;
    case EHFS_EROFS:
        return MAC_FS_WPR_ERR;
    default:
        return MAC_FS_IO_ERR;
    }
}

/* The longest eHFS name of a Mac name: each Mac Roman byte is at most 3 bytes of UTF-8. */
#define FROM_MAC_MAX (MAC_FS_NAME_MAX * 3)

/*
 * The eHFS name (UTF-8) of a name the Mac gives (Mac Roman), '/' as ':' (the
 * Mac's names have '/' where the HAL's have ':'). Control characters and ':'
 * are refused. Returns its length, or 0.
 */
static uint32_t ehfs_name(const uint8_t *name, uint32_t length, uint8_t out[FROM_MAC_MAX])
{
    uint32_t n = 0;

    if (length == 0 || length > MAC_FS_NAME_MAX)
        return 0;
    for (uint32_t i = 0; i < length; i++)
    {
        uint8_t c = name[i];

        if (c < 0x20 || c == 0x7F || c == ':')
            return 0;
        n += mac_roman_to_utf8(c == '/' ? ':' : c, out + n);
    }
    return n;
}

/* The Mac's name of an entry (Pascal string). */
static void mac_name(const mac_fs_ehfs_t *v, const ehfs_entry_t *e, uint8_t out[MAC_FS_NAME_MAX + 1])
{
    char name[EHFS_NAME_MAX + 1];

    if (e->cnid == EHFS_CNID_ROOT)
    {
        uint32_t n = 0;

        while (v->name[n] != '\0')
            n++;
        out[0] = (uint8_t)n;
        k_memcpy(out + 1, v->name, n);
        return;
    }
    k_memcpy(name, e->name, e->name_length);
    name[e->name_length] = '\0';
    mac_fs_paths_mac_name(name, (uint32_t)e->cnid, out);
}

static void fill(const mac_fs_ehfs_t *v, const ehfs_entry_t *e, mac_fs_item_t *item)
{
    k_memset(item, 0, sizeof *item);
    item->id = (uint32_t)e->cnid;
    item->parent = (uint32_t)e->parent;
    item->is_dir = e->is_dir;
    mac_name(v, e, item->name);
    if (e->is_dir)
    {
        item->valence = e->valence > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)e->valence;
    }
    else
    {
        /* The Mac's files end below 2 GB. */
        uint64_t data = ehfs_le64(e->data_fork + FORK_LENGTH), rsrc = ehfs_le64(e->rsrc_fork + FORK_LENGTH);

        item->data_length = data > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)data;
        item->rsrc_length = rsrc > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)rsrc;
    }
    k_memcpy(item->finfo, e->finder, 16);
    k_memcpy(item->fxinfo, e->finder + 16, 16);
    item->created = mac_time(e->times[0]);
    item->modified = mac_time(e->times[1]);
    item->backed_up = e->times[4] != 0 ? mac_time(e->times[4]) : 0;
    item->locked = (e->flags & 1u) != 0;
}

static uint64_t now(const mac_fs_ehfs_t *v)
{
    return v->clock != NULL ? v->clock() : 0;
}

static int op_get(void *ctx, uint32_t id, mac_fs_item_t *item)
{
    mac_fs_ehfs_t *v = ctx;

    if (id < EHFS_CNID_ROOT || ehfs_cat_get(v->fs, id, &entry) != EHFS_OK)
        return -1;
    fill(v, &entry, item);
    return 0;
}

static bool same_name(const uint8_t *a, const uint8_t *b, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++)
    {
        uint8_t x = a[i] >= 'a' && a[i] <= 'z' ? (uint8_t)(a[i] - 32) : a[i];
        uint8_t y = b[i] >= 'a' && b[i] <= 'z' ? (uint8_t)(b[i] - 32) : b[i];

        if (x != y)
            return false;
    }
    return true;
}

/*
 * The item the Mac calls name in parent: by its eHFS name when the Mac name
 * is one, else by the "#<CNID>" at its end (a name the Mac sees changed).
 */
static int op_lookup(void *ctx, uint32_t parent, const uint8_t *name, uint32_t length, mac_fs_item_t *item)
{
    mac_fs_ehfs_t *v = ctx;
    uint8_t converted[FROM_MAC_MAX];
    uint32_t n = ehfs_name(name, length, converted);

    if (n != 0 && ehfs_cat_lookup(v->fs, parent, converted, n, &entry) == EHFS_OK)
    {
        fill(v, &entry, item);
        if (item->name[0] == length && same_name(item->name + 1, name, length))
            return 0;
    }

    uint32_t at = length, id = 0, scale = 1;

    while (at > 0 && name[at - 1] >= '0' && name[at - 1] <= '9' && scale <= 1000000000u)
    {
        id += (uint32_t)(name[at - 1] - '0') * scale;
        scale *= 10;
        at--;
    }
    if (at == 0 || at == length || name[at - 1] != '#' || id < EHFS_CNID_ROOT)
        return -1;
    if (ehfs_cat_get(v->fs, id, &entry) != EHFS_OK || entry.parent != parent)
        return -1;
    fill(v, &entry, item);
    return item->name[0] == length && same_name(item->name + 1, name, length) ? 0 : -1;
}

typedef struct
{
    uint32_t index, seen;
    bool found;
    const mac_fs_ehfs_t *v;
    mac_fs_item_t *item;
} child_search_t;

static int child_visit(void *context, const ehfs_entry_t *e)
{
    child_search_t *search = context;

    if (++search->seen != search->index)
        return 0;
    fill(search->v, e, search->item);
    search->found = true;
    return 1;
}

static int op_child(void *ctx, uint32_t parent, uint32_t index, mac_fs_item_t *item)
{
    mac_fs_ehfs_t *v = ctx;
    child_search_t search = {index, 0, false, v, item};

    if (index == 0)
        return -1;
    ehfs_cat_list(v->fs, parent, child_visit, &search);
    return search.found ? 0 : -1;
}

static int32_t op_read(void *ctx, uint32_t id, bool resource_fork, uint32_t offset, void *buffer, uint32_t length)
{
    mac_fs_ehfs_t *v = ctx;
    uint32_t done = 0;
    int r = ehfs_read(v->fs, id, resource_fork ? EHFS_RESOURCE_FORK : EHFS_DATA_FORK, offset, buffer, length, &done);

    return r == EHFS_OK ? (int32_t)done : -1;
}

static int op_volume(void *ctx, mac_fs_volume_t *volume)
{
    mac_fs_ehfs_t *v = ctx;
    uint64_t files = ehfs_sb_get64(v->fs, SB_FILE_COUNT), folders = ehfs_sb_get64(v->fs, SB_FOLDER_COUNT);

    volume->total_bytes = v->fs->blocks * EHFS_BLOCK_SIZE;
    volume->free_bytes = ehfs_sb_get64(v->fs, SB_FREE_BLOCKS) * EHFS_BLOCK_SIZE;
    volume->files = files > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)files;
    volume->folders = folders > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)folders;
    volume->read_only = v->fs->read_only;
    return 0;
}

static int op_create(void *ctx, uint32_t parent, const uint8_t *name, uint32_t length, bool is_dir,
                     mac_fs_item_t *item)
{
    mac_fs_ehfs_t *v = ctx;
    uint8_t converted[FROM_MAC_MAX];
    uint32_t n = ehfs_name(name, length, converted);

    if (n == 0)
        return MAC_FS_BD_NAM_ERR;

    int r = ehfs_create(v->fs, parent, converted, n, is_dir, now(v), &entry);

    if (r == EHFS_ENOENT)
        return MAC_FS_DIR_NF_ERR;
    if (r != EHFS_OK)
        return os_error(r);
    fill(v, &entry, item);
    return 0;
}

static int op_remove(void *ctx, uint32_t id)
{
    mac_fs_ehfs_t *v = ctx;

    if (id <= EHFS_CNID_ROOT || ehfs_cat_get(v->fs, id, &entry) != EHFS_OK)
        return MAC_FS_FNF_ERR;
    if (entry.flags & 1u)
        return MAC_FS_FLCKD_ERR;
    return os_error(ehfs_remove(v->fs, entry.parent, entry.name, entry.name_length, now(v)));
}

static int op_move(void *ctx, uint32_t id, uint32_t new_parent, const uint8_t *name, uint32_t length)
{
    mac_fs_ehfs_t *v = ctx;
    uint8_t converted[FROM_MAC_MAX];
    const uint8_t *new_name;
    uint32_t n;

    if (id <= EHFS_CNID_ROOT || ehfs_cat_get(v->fs, id, &entry) != EHFS_OK)
        return MAC_FS_FNF_ERR;
    if (name != NULL)
    {
        n = ehfs_name(name, length, converted);
        if (n == 0)
            return MAC_FS_BD_NAM_ERR;
        new_name = converted;
    }
    else
    {
        n = entry.name_length;
        new_name = entry.name;
    }

    int r = ehfs_cat_get(v->fs, new_parent, &other);

    if (r != EHFS_OK || !other.is_dir)
        return MAC_FS_DIR_NF_ERR;
    r = ehfs_move(v->fs, entry.parent, entry.name, entry.name_length, new_parent, new_name, n, now(v));
    return r == EHFS_EINVAL ? MAC_FS_BAD_MOV_ERR : os_error(r);
}

static int op_set_info(void *ctx, const mac_fs_item_t *item)
{
    mac_fs_ehfs_t *v = ctx;

    if (ehfs_cat_get(v->fs, item->id, &entry) != EHFS_OK)
        return MAC_FS_FNF_ERR;
    k_memcpy(entry.finder, item->finfo, 16);
    k_memcpy(entry.finder + 16, item->fxinfo, 16);
    entry.times[0] = ehfs_time(item->created);
    entry.times[1] = ehfs_time(item->modified);
    entry.times[4] = item->backed_up != 0 ? ehfs_time(item->backed_up) : 0;
    entry.times[2] = now(v);
    entry.flags = (uint16_t)((entry.flags & ~1u) | (item->locked && !entry.is_dir ? 1u : 0u));
    return os_error(ehfs_set_info(v->fs, &entry));
}

static int32_t op_write(void *ctx, uint32_t id, bool resource_fork, uint32_t offset, const void *buffer,
                        uint32_t length)
{
    mac_fs_ehfs_t *v = ctx;
    int r = ehfs_write(v->fs, id, resource_fork ? EHFS_RESOURCE_FORK : EHFS_DATA_FORK, offset, buffer, length,
                       now(v));

    return r == EHFS_OK ? (int32_t)length : os_error(r);
}

static int op_set_length(void *ctx, uint32_t id, bool resource_fork, uint32_t length)
{
    mac_fs_ehfs_t *v = ctx;

    return os_error(ehfs_set_length(v->fs, id, resource_fork ? EHFS_RESOURCE_FORK : EHFS_DATA_FORK, length, now(v)));
}

void mac_fs_ehfs_init(mac_fs_ehfs_t *volume, ehfs_t *fs, mac_fs_ehfs_clock_t clock)
{
    k_memset(volume, 0, sizeof *volume);
    volume->fs = fs;
    volume->clock = clock;

    /* The volume's name in Mac Roman: characters it does not have, control characters and ':' as '_'. */
    const uint8_t *name = fs->sb + SB_NAME + 1, *end = name + fs->sb[SB_NAME];
    uint32_t n = 0;

    while (name < end && n < sizeof volume->name - 1)
    {
        int c = mac_roman_from_unicode(mac_roman_utf8_next(&name, end));

        if (c < 0x20 || c == 0x7F || c == ':')
            c = '_';
        volume->name[n++] = (char)c;
    }
    if (n == 0)
        volume->name[n++] = '_';
    volume->name[n] = '\0';

    volume->ops.ctx = volume;
    volume->ops.volume_name = volume->name;
    volume->ops.get = op_get;
    volume->ops.lookup = op_lookup;
    volume->ops.child = op_child;
    volume->ops.read = op_read;
    volume->ops.volume = op_volume;
    if (!fs->read_only)
    {
        volume->ops.create = op_create;
        volume->ops.remove = op_remove;
        volume->ops.move = op_move;
        volume->ops.set_info = op_set_info;
        volume->ops.write = op_write;
        volume->ops.set_length = op_set_length;
    }
}
