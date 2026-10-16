/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hfs.h"

#include "kernel/lib/k_memory.h"

/*
 * Layout constants and field offsets: Inside Macintosh: Files, chapter 2
 * (Data Organization on Volumes). All numbers are big-endian. The values are
 * written from memory of that chapter and checked against volumes made by
 * hfsutils (tools/make_hfs_image.sh).
 */

#define BLOCK_SIZE 512          /* logical block */
#define MDB_BLOCK 2             /* Master Directory Block */
#define MDB_SIGNATURE 0x4244    /* 'BD' */

#define MDB_ALLOC_BLOCKS 18
#define MDB_ALLOC_BLOCK_SIZE 20
#define MDB_ALLOC_START 28
#define MDB_FREE_BLOCKS 34
#define MDB_VOLUME_NAME 36      /* Str27 */
#define MDB_FOLDER_COUNT 88
#define MDB_FILE_COUNT_32 84
#define MDB_EXTENTS_LENGTH 130
#define MDB_EXTENTS_EXTENTS 134 /* 3 extent descriptors */
#define MDB_CATALOG_LENGTH 146
#define MDB_CATALOG_EXTENTS 150

#define NODE_SIZE 512
#define NODE_HEADER_SIZE 14
#define NODE_LINK_FORWARD 0
#define NODE_TYPE 8
#define NODE_RECORDS 10
#define NODE_TYPE_LEAF 0xFF     /* -1 */
#define NODE_TYPE_HEADER 1

#define HEADER_FIRST_LEAF (NODE_HEADER_SIZE + 10)
#define HEADER_NODE_SIZE (NODE_HEADER_SIZE + 18)

#define CATALOG_FOLDER 1
#define CATALOG_FILE 2

#define FORK_DATA 0x00
#define FORK_RESOURCE 0xFF

/* A fork being read: its first three extents, and whether more may be in
 * the extents overflow tree. */
typedef struct
{
    uint8_t kind;
    uint32_t file_id;
    uint32_t length;
    hfs_extent_t extents[3];
    bool use_overflow;
} fork_t;

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 |
           p[3];
}

static void parse_extents(const uint8_t *p, hfs_extent_t extents[3])
{
    for (int i = 0; i < 3; i++)
    {
        extents[i].start = be16(p + 4 * i);
        extents[i].count = be16(p + 4 * i + 2);
    }
}

static char fold(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

bool hfs_names_equal(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0')
    {
        if (fold(*a++) != fold(*b++))
            return false;
    }

    return *a == *b;
}

/* Read len bytes at byte offset within the volume. */
static int volume_read(const hfs_volume_t *volume, uint64_t offset,
                       void *buffer, uint32_t length)
{
    uint8_t block[BLOCK_SIZE];
    uint8_t *out = buffer;

    while (length > 0)
    {
        uint32_t inner = (uint32_t)(offset % BLOCK_SIZE);
        uint32_t chunk = BLOCK_SIZE - inner;

        if (chunk > length)
            chunk = length;

        if (block_read(volume->device, volume->base + offset / BLOCK_SIZE, 1,
                       block) != 0)
            return -1;

        k_memcpy(out, block + inner, chunk);
        out += chunk;
        offset += chunk;
        length -= chunk;
    }

    return 0;
}

static int btree_walk(const hfs_volume_t *volume, const fork_t *tree,
                      uint32_t first_leaf,
                      int (*visit)(const uint8_t *key, const uint8_t *data,
                                   uint32_t data_size, void *context),
                      void *context);

static int fork_read_raw(const hfs_volume_t *volume, const fork_t *fork,
                         uint32_t offset, void *buffer, uint32_t length);

/*
 * Find the extents record for (kind, file_id) that starts at fork block
 * first_block, in the extents overflow tree. Leaf records are scanned in
 * order; the key is {len 7, fork type, file number (4), start block (2)}.
 */
typedef struct
{
    uint8_t kind;
    uint32_t file_id;
    uint32_t first_block;
    hfs_extent_t extents[3];
    bool found;
} overflow_search_t;

static int overflow_visit(const uint8_t *key, const uint8_t *data,
                          uint32_t data_size, void *context)
{
    overflow_search_t *search = context;

    if (key[0] != 7 || data_size < 12)
        return 0;

    if (key[1] == search->kind && be32(key + 2) == search->file_id &&
        be16(key + 6) == search->first_block)
    {
        parse_extents(data, search->extents);
        search->found = true;

        return 1;
    }

    return 0;
}

static fork_t overflow_tree(const hfs_volume_t *volume)
{
    fork_t tree = {0, 0, volume->overflow_length, {{0, 0}}, false};

    k_memcpy(tree.extents, volume->overflow_extents, sizeof(tree.extents));

    return tree;
}

/* Allocation block of the volume that holds fork block fork_block. */
static int fork_block_to_volume(const hfs_volume_t *volume, const fork_t *fork,
                                uint32_t fork_block, uint32_t *volume_block)
{
    uint32_t covered = 0;
    hfs_extent_t extents[3];

    k_memcpy(extents, fork->extents, sizeof(extents));

    for (;;)
    {
        for (int i = 0; i < 3; i++)
        {
            if (fork_block < covered + extents[i].count)
            {
                *volume_block = extents[i].start + (fork_block - covered);

                return 0;
            }

            covered += extents[i].count;
        }

        if (!fork->use_overflow || covered == 0)
            return -1;

        overflow_search_t search = {fork->kind, fork->file_id, covered,
                                    {{0, 0}}, false};
        fork_t tree = overflow_tree(volume);

        if (btree_walk(volume, &tree, volume->overflow_first_leaf,
                       overflow_visit, &search) < 0 || !search.found)
            return -1;

        k_memcpy(extents, search.extents, sizeof(extents));
    }
}

/* Read from a fork; the range has been checked against the fork length. */
static int fork_read_raw(const hfs_volume_t *volume, const fork_t *fork,
                         uint32_t offset, void *buffer, uint32_t length)
{
    uint8_t *out = buffer;

    while (length > 0)
    {
        uint32_t fork_block = offset / volume->alloc_block_size;
        uint32_t inner = offset % volume->alloc_block_size;
        uint32_t chunk = volume->alloc_block_size - inner;
        uint32_t volume_block;

        if (chunk > length)
            chunk = length;

        if (fork_block_to_volume(volume, fork, fork_block, &volume_block) != 0)
            return -1;

        uint64_t byte = (uint64_t)volume->alloc_start * BLOCK_SIZE +
                        (uint64_t)volume_block * volume->alloc_block_size +
                        inner;

        if (volume_read(volume, byte, out, chunk) != 0)
            return -1;

        out += chunk;
        offset += chunk;
        length -= chunk;
    }

    return 0;
}

/*
 * Visit every record of every leaf node, following the forward links from
 * first_leaf. visit gets the key, the data after it and the bytes available
 * for the data; it returns 0 to go on, 1 to stop (found), <0 for an error.
 * Returns 1 if stopped by visit, 0 at the end, -1 on error.
 */
static int btree_walk(const hfs_volume_t *volume, const fork_t *tree,
                      uint32_t first_leaf,
                      int (*visit)(const uint8_t *key, const uint8_t *data,
                                   uint32_t data_size, void *context),
                      void *context)
{
    uint8_t node[NODE_SIZE];
    uint32_t node_total = tree->length / NODE_SIZE;
    uint32_t number = first_leaf;

    /* A damaged link loop must end: no tree has more leaves than nodes. */
    for (uint32_t visited = 0; number != 0; visited++)
    {
        if (visited >= node_total || number >= node_total ||
            fork_read_raw(volume, tree, number * NODE_SIZE, node,
                          NODE_SIZE) != 0 ||
            node[NODE_TYPE] != NODE_TYPE_LEAF)
            return -1;

        uint32_t records = be16(node + NODE_RECORDS);

        if (NODE_HEADER_SIZE + 2 * (records + 1) > NODE_SIZE)
            return -1;

        for (uint32_t i = 0; i < records; i++)
        {
            uint32_t start = be16(node + NODE_SIZE - 2 * (i + 1));
            uint32_t end = be16(node + NODE_SIZE - 2 * (i + 2));

            if (start < NODE_HEADER_SIZE || end < start ||
                end > NODE_SIZE - 2 * (records + 1) || start >= end)
                return -1;

            uint32_t key_size = node[start];
            uint32_t data_at = (start + 1 + key_size + 1) & ~1u;

            if (key_size == 0 || data_at > end)
                continue;   /* deleted or empty record */

            int result = visit(node + start, node + data_at, end - data_at,
                               context);

            if (result != 0)
                return result;
        }

        number = be32(node + NODE_LINK_FORWARD);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Catalog                                                             */
/* ------------------------------------------------------------------ */

/*
 * The catalog file's first three extents are in the MDB; the rest are in the
 * extents overflow tree under its reserved file number (4). The extents file
 * itself (number 3) can only use the three in the MDB.
 */
#define CATALOG_FILE_ID 4

static fork_t catalog_tree(const hfs_volume_t *volume)
{
    fork_t tree = {FORK_DATA, CATALOG_FILE_ID, volume->catalog_length,
                   {{0, 0}}, true};

    k_memcpy(tree.extents, volume->catalog_extents, sizeof(tree.extents));

    return tree;
}

/* Catalog leaf key: {len, reserved, parent ID (4), Str31 name}. */
static bool catalog_key(const uint8_t *key, uint32_t *parent,
                        const uint8_t **name, uint32_t *name_length)
{
    if (key[0] < 6)
        return false;

    *parent = be32(key + 2);
    *name_length = key[6];
    *name = key + 7;

    return *name_length <= HFS_NAME_MAX && 7 + *name_length <= 1u + key[0];
}

static bool make_entry(const uint8_t *key, const uint8_t *data,
                       uint32_t data_size, hfs_entry_t *entry)
{
    uint32_t parent, name_length;
    const uint8_t *name;

    if (!catalog_key(key, &parent, &name, &name_length) || data_size < 2)
        return false;

    k_memset(entry, 0, sizeof(*entry));

    for (uint32_t i = 0; i < name_length; i++)
        entry->name[i] = (char)name[i];

    entry->parent_id = parent;

    if (data[0] == CATALOG_FOLDER && data_size >= 10)
    {
        entry->is_folder = true;
        entry->valence = be16(data + 4);
        entry->id = be32(data + 6);

        return true;
    }

    if (data[0] == CATALOG_FILE && data_size >= 98)
    {
        entry->id = be32(data + 20);
        entry->file_type = be32(data + 4);
        entry->file_creator = be32(data + 8);
        entry->data_length = be32(data + 26);
        entry->rsrc_length = be32(data + 36);
        parse_extents(data + 74, entry->data_extents);
        parse_extents(data + 86, entry->rsrc_extents);

        return true;
    }

    return false;   /* thread records and anything unknown */
}

typedef struct
{
    uint32_t parent;
    const char *name;
    hfs_entry_t *entry;
} lookup_search_t;

static int lookup_visit(const uint8_t *key, const uint8_t *data,
                        uint32_t data_size, void *context)
{
    lookup_search_t *search = context;
    hfs_entry_t candidate;

    if (!make_entry(key, data, data_size, &candidate) ||
        candidate.parent_id != search->parent ||
        !hfs_names_equal(candidate.name, search->name))
        return 0;

    *search->entry = candidate;

    return 1;
}

static int find_child(const hfs_volume_t *volume, uint32_t parent,
                      const char *name, hfs_entry_t *entry)
{
    lookup_search_t search = {parent, name, entry};
    fork_t tree = catalog_tree(volume);
    int result = btree_walk(volume, &tree, volume->catalog_first_leaf,
                            lookup_visit, &search);

    return result == 1 ? 0 : -1;
}

int hfs_lookup(hfs_volume_t *volume, const char *path, hfs_entry_t *entry)
{
    if (volume == NULL || path == NULL || entry == NULL)
        return -1;

    k_memset(entry, 0, sizeof(*entry));
    entry->is_folder = true;
    entry->id = HFS_ROOT_ID;
    entry->parent_id = 1;
    k_memcpy(entry->name, volume->name, sizeof(volume->name));

    while (*path != '\0')
    {
        char component[HFS_NAME_MAX + 1];
        uint32_t length = 0;

        if (*path == ':')
        {
            path++;
            continue;
        }

        while (*path != '\0' && *path != ':')
        {
            if (length == HFS_NAME_MAX)
                return -1;

            component[length++] = *path++;
        }

        component[length] = '\0';

        if (!entry->is_folder ||
            find_child(volume, entry->id, component, entry) != 0)
            return -1;
    }

    return 0;
}

typedef struct
{
    uint32_t folder;
    hfs_list_fn fn;
    void *context;
} list_search_t;

static int list_visit(const uint8_t *key, const uint8_t *data,
                      uint32_t data_size, void *context)
{
    list_search_t *search = context;
    hfs_entry_t entry;

    if (!make_entry(key, data, data_size, &entry) ||
        entry.parent_id != search->folder)
        return 0;

    return search->fn(&entry, search->context) != 0 ? 1 : 0;
}

int hfs_list(hfs_volume_t *volume, uint32_t folder_id, hfs_list_fn fn,
             void *context)
{
    if (volume == NULL || fn == NULL)
        return -1;

    list_search_t search = {folder_id, fn, context};
    fork_t tree = catalog_tree(volume);

    return btree_walk(volume, &tree, volume->catalog_first_leaf, list_visit,
                      &search) < 0 ? -1 : 0;
}

int hfs_read(hfs_volume_t *volume, const hfs_entry_t *entry,
             hfs_fork_kind_t fork, uint32_t offset, void *buffer,
             uint32_t length)
{
    if (volume == NULL || entry == NULL || buffer == NULL || entry->is_folder)
        return -1;

    fork_t source;

    source.kind = fork == HFS_FORK_DATA ? FORK_DATA : FORK_RESOURCE;
    source.file_id = entry->id;
    source.length = fork == HFS_FORK_DATA ? entry->data_length
                                          : entry->rsrc_length;
    source.use_overflow = true;
    k_memcpy(source.extents,
             fork == HFS_FORK_DATA ? entry->data_extents : entry->rsrc_extents,
             sizeof(source.extents));

    if (offset >= source.length)
        return 0;

    if (length > source.length - offset)
        length = source.length - offset;

    if (length > 0x7FFFFFFF ||
        fork_read_raw(volume, &source, offset, buffer, length) != 0)
        return -1;

    return (int)length;
}

/* ------------------------------------------------------------------ */
/* Mounting                                                            */
/* ------------------------------------------------------------------ */

/* Node size and first leaf from the header node (node 0) of a tree. */
static int read_tree_header(const hfs_volume_t *volume, const fork_t *tree,
                            uint32_t *first_leaf)
{
    uint8_t node[NODE_SIZE];

    if (tree->length < 2 * NODE_SIZE ||
        fork_read_raw(volume, tree, 0, node, NODE_SIZE) != 0 ||
        node[NODE_TYPE] != NODE_TYPE_HEADER ||
        be16(node + HEADER_NODE_SIZE) != NODE_SIZE)
        return -1;

    *first_leaf = be32(node + HEADER_FIRST_LEAF);

    return 0;
}

int hfs_mount(hfs_volume_t *volume, block_device_t *device, uint64_t start,
              uint64_t block_count)
{
    uint8_t mdb[BLOCK_SIZE];

    if (volume == NULL || device == NULL || device->block_size != BLOCK_SIZE ||
        start >= device->block_count)
        return -1;

    k_memset(volume, 0, sizeof(*volume));
    volume->device = device;
    volume->base = start;

    if (block_read(device, start + MDB_BLOCK, 1, mdb) != 0 ||
        be16(mdb) != MDB_SIGNATURE)
        return -1;

    volume->alloc_block_size = be32(mdb + MDB_ALLOC_BLOCK_SIZE);
    volume->alloc_start = be16(mdb + MDB_ALLOC_START);
    volume->alloc_block_count = be16(mdb + MDB_ALLOC_BLOCKS);
    volume->free_alloc_blocks = be16(mdb + MDB_FREE_BLOCKS);
    volume->file_count = be32(mdb + MDB_FILE_COUNT_32);
    volume->folder_count = be32(mdb + MDB_FOLDER_COUNT);

    if (volume->alloc_block_size == 0 ||
        volume->alloc_block_size % BLOCK_SIZE != 0 ||
        volume->alloc_block_size > 0x1000000)
        return -1;

    /* The allocation blocks must lie inside the volume (and the device). */
    uint64_t end = volume->alloc_start +
                   (uint64_t)volume->alloc_block_count *
                       (volume->alloc_block_size / BLOCK_SIZE);

    if (end > device->block_count - start ||
        (block_count != 0 && end > block_count))
        return -1;

    uint32_t name_length = mdb[MDB_VOLUME_NAME];

    if (name_length > HFS_VOLUME_NAME_MAX)
        return -1;

    k_memcpy(volume->name, mdb + MDB_VOLUME_NAME + 1, name_length);

    volume->overflow_length = be32(mdb + MDB_EXTENTS_LENGTH);
    parse_extents(mdb + MDB_EXTENTS_EXTENTS, volume->overflow_extents);
    volume->catalog_length = be32(mdb + MDB_CATALOG_LENGTH);
    parse_extents(mdb + MDB_CATALOG_EXTENTS, volume->catalog_extents);

    fork_t catalog = catalog_tree(volume);
    fork_t overflow = overflow_tree(volume);

    if (read_tree_header(volume, &catalog, &volume->catalog_first_leaf) != 0 ||
        read_tree_header(volume, &overflow, &volume->overflow_first_leaf) != 0)
        return -1;

    return 0;
}
