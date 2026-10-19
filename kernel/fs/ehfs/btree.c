/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * B+-trees (docs/ehfs.md section 4). Node 0 of a tree's file is the header
 * node (root, depth, counts, a map of the nodes in use). Every other node in
 * use is an index node (records: separator key -> child node number) or a
 * leaf (records: key -> value); the nodes of one level are linked in key
 * order (next/prev). A record is a u16 key length, the key, a u16 value
 * length and the value; records are packed from NODE_DATA up and their
 * offsets are a u16 table at the end of the node, in key order.
 *
 * The separator of a child is its first key when it was split off; the
 * leftmost child of a new root gets the empty key. A search goes to the last
 * child whose separator is not above the key, or to the first child. Nodes
 * are not merged: a node is freed when it is empty, and a root with one
 * child is replaced by the child. A tree's file grows by doubling (at most 8
 * runs of blocks, the superblock's fork descriptor).
 *
 * Buffers: fs->node[0] the node being worked on, [1] another node of the
 * same level, [2] the header node, [3] fsck's map.
 */

#include "btree.h"

#include "kernel/lib/k_memory.h"

#define MAX_DEPTH 16
#define REC_OVERHEAD 6u         /* key length, value length, offset entry */
#define HALF ((EHFS_NODE_SIZE - NODE_DATA) / 2u)

/* ---- Nodes ---- */

static int node_io(ehfs_t *fs, int tree, uint64_t n, uint8_t *node, bool write)
{
    const uint8_t *fork = ehfs_sb_fork(fs, tree);

    if (n >= ehfs_le64(fork + FORK_BLOCKS) / EHFS_NODE_BLOCKS)
        return EHFS_ECORRUPT;
    if (write)
        ehfs_node_seal(node);
    for (uint32_t k = 0; k < EHFS_NODE_BLOCKS; k++)
    {
        uint64_t b;
        int r = ehfs_fork_inline_block(fork, n * EHFS_NODE_BLOCKS + k, &b);

        if (r == EHFS_OK)
            r = write ? ehfs_write_meta(fs, b, node + k * EHFS_BLOCK_SIZE)
                      : ehfs_read_block(fs, b, node + k * EHFS_BLOCK_SIZE);
        if (r != EHFS_OK)
            return r;
    }
    if (!write && !ehfs_node_intact(node))
        return EHFS_ECORRUPT;
    return EHFS_OK;
}

static int read_node(ehfs_t *fs, int tree, uint64_t n, uint8_t *node)
{
    int r = node_io(fs, tree, n, node, false);

    if (r == EHFS_OK && n != 0 && node[NODE_KIND] != EHFS_NODE_INDEX && node[NODE_KIND] != EHFS_NODE_LEAF)
        r = EHFS_ECORRUPT;
    return r;
}

static int write_node(ehfs_t *fs, int tree, uint64_t n, uint8_t *node)
{
    return node_io(fs, tree, n, node, true);
}

static uint32_t count_of(const uint8_t *node)
{
    return ehfs_le16(node + NODE_COUNT);
}

static uint32_t offset_of(const uint8_t *node, uint32_t i)
{
    return ehfs_le16(node + EHFS_NODE_SIZE - 2 * (i + 1));
}

static void set_offset(uint8_t *node, uint32_t i, uint32_t offset)
{
    ehfs_put16(node + EHFS_NODE_SIZE - 2 * (i + 1), (uint16_t)offset);
}

static const uint8_t *key_of(const uint8_t *node, uint32_t i, uint32_t *length)
{
    uint32_t o = offset_of(node, i);

    *length = ehfs_le16(node + o);
    return node + o + 2;
}

static const uint8_t *value_of(const uint8_t *node, uint32_t i, uint32_t *length)
{
    uint32_t o = offset_of(node, i);
    uint32_t k = ehfs_le16(node + o);

    *length = ehfs_le16(node + o + 2 + k);
    return node + o + 4 + k;
}

static uint32_t size_of(const uint8_t *node, uint32_t i)
{
    uint32_t k, v;

    key_of(node, i, &k);
    value_of(node, i, &v);
    return 4 + k + v;
}

static uint32_t free_space(const uint8_t *node)
{
    return EHFS_NODE_SIZE - 2 * count_of(node) - ehfs_le16(node + NODE_FREE);
}

static uint64_t child_of(const uint8_t *node, uint32_t i)
{
    uint32_t length;

    return ehfs_le64(value_of(node, i, &length));
}

static int compare(const uint8_t *a, uint32_t a_length, const uint8_t *b, uint32_t b_length)
{
    uint32_t n = a_length < b_length ? a_length : b_length;
    int c = n > 0 ? k_memcmp(a, b, n) : 0;

    if (c != 0)
        return c;
    return a_length < b_length ? -1 : a_length > b_length ? 1 : 0;
}

/* The first record whose key is >= key (count_of when there is none); *exact if equal. */
static uint32_t lower_bound(const uint8_t *node, const uint8_t *key, uint32_t length, bool *exact)
{
    uint32_t lo = 0, hi = count_of(node);

    *exact = false;
    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2, k;
        const uint8_t *mk = key_of(node, mid, &k);
        int c = compare(mk, k, key, length);

        if (c == 0)
        {
            *exact = true;
            return mid;
        }
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* The child of an index node to follow for key. */
static uint32_t child_index(const uint8_t *node, const uint8_t *key, uint32_t length)
{
    bool exact;
    uint32_t i = lower_bound(node, key, length, &exact);

    return exact ? i : i > 0 ? i - 1 : 0;
}

static void node_init(uint8_t *node, uint8_t kind, uint8_t height)
{
    k_memset(node, 0, EHFS_NODE_SIZE);
    node[NODE_KIND] = kind;
    node[NODE_HEIGHT] = height;
    ehfs_put16(node + NODE_FREE, NODE_DATA);
}

/* Put a record at position i; the caller made sure it fits. */
static void node_insert(uint8_t *node, uint32_t i, const uint8_t *key, uint32_t key_length, const void *value,
                        uint32_t value_length)
{
    uint32_t count = count_of(node);
    uint32_t at = ehfs_le16(node + NODE_FREE);

    ehfs_put16(node + at, (uint16_t)key_length);
    k_memcpy(node + at + 2, key, key_length);
    ehfs_put16(node + at + 2 + key_length, (uint16_t)value_length);
    k_memcpy(node + at + 4 + key_length, value, value_length);
    for (uint32_t j = count; j > i; j--)
        set_offset(node, j, offset_of(node, j - 1));
    set_offset(node, i, at);
    ehfs_put16(node + NODE_COUNT, (uint16_t)(count + 1));
    ehfs_put16(node + NODE_FREE, (uint16_t)(at + 4 + key_length + value_length));
}

/* Take record i out and close the gap. */
static void node_remove(uint8_t *node, uint32_t i)
{
    uint32_t count = count_of(node);
    uint32_t at = offset_of(node, i);
    uint32_t size = size_of(node, i);
    uint32_t end = ehfs_le16(node + NODE_FREE);

    for (uint32_t j = at; j + size < end; j++)     /* down, lowest byte first: the ranges overlap */
        node[j] = node[j + size];
    for (uint32_t j = i; j + 1 < count; j++)
        set_offset(node, j, offset_of(node, j + 1));
    for (uint32_t j = 0; j + 1 < count; j++)
    {
        uint32_t o = offset_of(node, j);

        if (o > at)
            set_offset(node, j, o - size);
    }
    set_offset(node, count - 1, 0);
    ehfs_put16(node + NODE_COUNT, (uint16_t)(count - 1));
    ehfs_put16(node + NODE_FREE, (uint16_t)(end - size));
}

/* ---- The header node and the node map ---- */

static int read_header(ehfs_t *fs, int tree)
{
    int r = node_io(fs, tree, 0, fs->node[2], false);

    if (r == EHFS_OK && fs->node[2][NODE_KIND] != EHFS_NODE_HEADER)
        r = EHFS_ECORRUPT;
    return r;
}

static int write_header(ehfs_t *fs, int tree)
{
    return write_node(fs, tree, 0, fs->node[2]);
}

/* Double the tree's file (more runs of blocks in the superblock's fork descriptor). */
static int grow(ehfs_t *fs, int tree)
{
    uint8_t *hdr = fs->node[2];
    uint8_t *fork = ehfs_sb_fork(fs, tree);
    uint64_t nodes = ehfs_le64(hdr + HDR_NODES);
    uint64_t more = nodes < EHFS_MAX_NODES - nodes ? nodes : EHFS_MAX_NODES - nodes;
    uint64_t need = more * EHFS_NODE_BLOCKS;

    if (more == 0)
        return EHFS_ENOSPC;
    while (need > 0)
    {
        uint32_t used = 0;
        uint64_t last_end = 0, start, count;

        for (; used < EHFS_INLINE_EXTENTS && ehfs_le64(fork + FORK_EXTENT + 16 * used + 8) != 0; used++)
            last_end = ehfs_le64(fork + FORK_EXTENT + 16 * used) + ehfs_le64(fork + FORK_EXTENT + 16 * used + 8);

        int r = ehfs_alloc(fs, last_end, need, &start, &count);

        if (r != EHFS_OK)
            return r;
        if (used > 0 && start == last_end)
        {
            uint8_t *e = fork + FORK_EXTENT + 16 * (used - 1);

            ehfs_put64(e + 8, ehfs_le64(e + 8) + count);
        }
        else if (used < EHFS_INLINE_EXTENTS)
        {
            ehfs_put64(fork + FORK_EXTENT + 16 * used, start);
            ehfs_put64(fork + FORK_EXTENT + 16 * used + 8, count);
        }
        else
        {
            ehfs_free(fs, start, count);
            return EHFS_ENOSPC;     /* the tree's file has no room for another run */
        }
        ehfs_put64(fork + FORK_BLOCKS, ehfs_le64(fork + FORK_BLOCKS) + count);
        ehfs_put64(fork + FORK_LENGTH, ehfs_le64(fork + FORK_BLOCKS) * EHFS_BLOCK_SIZE);
        fs->sb_dirty = true;
        need -= count;
    }
    ehfs_put64(hdr + HDR_NODES, nodes + more);
    ehfs_put64(hdr + HDR_FREE_NODES, ehfs_le64(hdr + HDR_FREE_NODES) + more);
    return EHFS_OK;
}

static int alloc_node(ehfs_t *fs, int tree, uint64_t *n)
{
    uint8_t *hdr = fs->node[2];

    if (ehfs_le64(hdr + HDR_FREE_NODES) == 0)
    {
        int r = grow(fs, tree);

        if (r != EHFS_OK)
            return r;
    }

    uint64_t nodes = ehfs_le64(hdr + HDR_NODES);

    for (uint64_t i = 1; i < nodes; i++)
    {
        uint8_t *byte = &hdr[HDR_MAP + i / 8];

        if (!(*byte & (1u << (i % 8))))
        {
            *byte |= (uint8_t)(1u << (i % 8));
            ehfs_put64(hdr + HDR_FREE_NODES, ehfs_le64(hdr + HDR_FREE_NODES) - 1);
            *n = i;
            return EHFS_OK;
        }
    }
    return EHFS_ECORRUPT;       /* the free count says there is one */
}

static void free_node(ehfs_t *fs, uint64_t n)
{
    uint8_t *hdr = fs->node[2];

    hdr[HDR_MAP + n / 8] &= (uint8_t)~(1u << (n % 8));
    ehfs_put64(hdr + HDR_FREE_NODES, ehfs_le64(hdr + HDR_FREE_NODES) + 1);
}

/* ---- Searching ---- */

typedef struct
{
    uint64_t node[MAX_DEPTH];       /* [0] the root .. [depth - 1] the leaf */
    uint32_t index[MAX_DEPTH];      /* the child followed in node[level] */
    uint32_t depth;
} path_t;

/* Go from the root to the leaf for key, into fs->node[0]. The header must be in fs->node[2]. */
static int descend(ehfs_t *fs, int tree, const uint8_t *key, uint32_t length, path_t *path)
{
    uint8_t *node = fs->node[0];
    uint64_t n = ehfs_le64(fs->node[2] + HDR_ROOT);
    uint32_t depth = ehfs_le32(fs->node[2] + HDR_DEPTH);

    if (depth == 0 || depth > MAX_DEPTH)
        return EHFS_ECORRUPT;
    path->depth = depth;
    for (uint32_t level = 0; level < depth; level++)
    {
        int r = read_node(fs, tree, n, node);

        if (r != EHFS_OK)
            return r;
        if (node[NODE_HEIGHT] != depth - level || count_of(node) == 0 ||
            (level + 1 < depth) != (node[NODE_KIND] == EHFS_NODE_INDEX))
            return EHFS_ECORRUPT;
        path->node[level] = n;
        if (level + 1 < depth)
        {
            path->index[level] = child_index(node, key, length);
            n = child_of(node, path->index[level]);
        }
    }
    return EHFS_OK;
}

int ehfs_bt_find(ehfs_t *fs, int tree, const uint8_t *key, uint32_t key_length, void *value, uint32_t value_size,
                 uint32_t *value_length)
{
    path_t path;
    bool exact;
    int r = read_header(fs, tree);

    if (r != EHFS_OK)
        return r;
    if (ehfs_le64(fs->node[2] + HDR_ROOT) == 0)
        return EHFS_ENOENT;
    r = descend(fs, tree, key, key_length, &path);
    if (r != EHFS_OK)
        return r;

    uint32_t i = lower_bound(fs->node[0], key, key_length, &exact);

    if (!exact)
        return EHFS_ENOENT;

    uint32_t length;
    const uint8_t *v = value_of(fs->node[0], i, &length);

    if (value != NULL)
        k_memcpy(value, v, length < value_size ? length : value_size);
    if (value_length != NULL)
        *value_length = length;
    return EHFS_OK;
}

uint64_t ehfs_bt_records(ehfs_t *fs, int tree)
{
    return read_header(fs, tree) == EHFS_OK ? ehfs_le64(fs->node[2] + HDR_RECORDS) : 0;
}

/* ---- Inserting ---- */

/* Link node `n` (in fs->node[1]) after node `left` (in fs->node[0]) on their level. */
static int link_after(ehfs_t *fs, int tree, uint64_t left, uint64_t n, bool leaf)
{
    uint8_t *l = fs->node[0], *r = fs->node[1];
    uint64_t next = ehfs_le64(l + NODE_NEXT);

    ehfs_put64(r + NODE_PREV, left);
    ehfs_put64(r + NODE_NEXT, next);
    ehfs_put64(l + NODE_NEXT, n);
    if (next != 0)
    {
        /* The node after them: through fs->block-sized steps would be awkward; use node[3]. */
        int e = read_node(fs, tree, next, fs->node[3]);

        if (e != EHFS_OK)
            return e;
        ehfs_put64(fs->node[3] + NODE_PREV, n);
        e = write_node(fs, tree, next, fs->node[3]);
        if (e != EHFS_OK)
            return e;
    }
    else if (leaf)
    {
        ehfs_put64(fs->node[2] + HDR_LAST_LEAF, n);
    }
    return EHFS_OK;
}

/*
 * Put (key, value) into node path->node[level] (read into fs->node[0]) at
 * its place, splitting the node and going up as needed.
 */
static int insert_at(ehfs_t *fs, int tree, path_t *path, int level, const uint8_t *key, uint32_t key_length,
                     const void *value, uint32_t value_length)
{
    uint8_t sep[EHFS_BT_KEY_MAX];
    uint8_t child[8];

    for (;;)
    {
        uint8_t *node = fs->node[0];
        uint64_t n = path->node[level];
        bool exact;
        uint32_t i = lower_bound(node, key, key_length, &exact);
        bool leaf = node[NODE_KIND] == EHFS_NODE_LEAF;

        if (free_space(node) >= key_length + value_length + REC_OVERHEAD)
        {
            node_insert(node, i, key, key_length, value, value_length);
            return write_node(fs, tree, n, node);
        }

        /*
         * Split: the node's records and the new one, in order, are shared out between this node and a
         * new one after it, about half the bytes each (both get at least one record).
         */
        uint64_t right;
        int r = alloc_node(fs, tree, &right);

        if (r != EHFS_OK)
            return r;

        uint8_t *copy = fs->node[3], *rn = fs->node[1];
        uint32_t count = count_of(node), total = key_length + value_length + 6, at = 0, sum = 0;
        uint64_t prev = ehfs_le64(node + NODE_PREV), next = ehfs_le64(node + NODE_NEXT);

        k_memcpy(copy, node, EHFS_NODE_SIZE);
        for (uint32_t j = 0; j < count; j++)
            total += size_of(copy, j) + 2;
        /* at = how many of the count + 1 records stay here */
        while (at < count && sum + (at == i ? key_length + value_length + 6 : size_of(copy, at < i ? at : at - 1) + 2) <= total / 2)
        {
            sum += at == i ? key_length + value_length + 6 : size_of(copy, at < i ? at : at - 1) + 2;
            at++;
        }
        if (at == 0)
            at = 1;
        node_init(node, copy[NODE_KIND], copy[NODE_HEIGHT]);
        node_init(rn, copy[NODE_KIND], copy[NODE_HEIGHT]);
        ehfs_put64(node + NODE_PREV, prev);
        ehfs_put64(node + NODE_NEXT, next);
        for (uint32_t j = 0; j <= count; j++)
        {
            uint8_t *to = j < at ? node : rn;
            uint32_t pos = j < at ? j : j - at;

            if (j == i)
            {
                node_insert(to, pos, key, key_length, value, value_length);
            }
            else
            {
                uint32_t src = j < i ? j : j - 1, k, v;
                const uint8_t *kp = key_of(copy, src, &k);
                const uint8_t *vp = value_of(copy, src, &v);

                node_insert(to, pos, kp, k, vp, v);
            }
        }

        uint32_t fk;
        const uint8_t *first;

        first = key_of(rn, 0, &fk);
        k_memcpy(sep, first, fk);

        r = link_after(fs, tree, n, right, leaf);
        if (r == EHFS_OK)
            r = write_node(fs, tree, n, node);
        if (r == EHFS_OK)
            r = write_node(fs, tree, right, rn);
        if (r != EHFS_OK)
            return r;

        ehfs_put64(child, right);
        if (level == 0)
        {
            /* A new root above the two. */
            uint64_t root;
            uint8_t left[8];

            r = alloc_node(fs, tree, &root);
            if (r != EHFS_OK)
                return r;
            ehfs_put64(left, n);
            node_init(node, EHFS_NODE_INDEX, (uint8_t)(path->depth + 1));
            node_insert(node, 0, sep, 0, left, 8);
            node_insert(node, 1, sep, fk, child, 8);
            ehfs_put64(fs->node[2] + HDR_ROOT, root);
            ehfs_put32(fs->node[2] + HDR_DEPTH, path->depth + 1);
            return write_node(fs, tree, root, node);
        }

        /* Up one level: the separator and the new node into the parent. */
        level--;
        r = read_node(fs, tree, path->node[level], node);
        if (r != EHFS_OK)
            return r;
        key = sep;
        key_length = fk;
        value = child;
        value_length = 8;
    }
}

int ehfs_bt_insert(ehfs_t *fs, int tree, const uint8_t *key, uint32_t key_length, const void *value,
                   uint32_t value_length)
{
    path_t path;
    bool exact;
    int r;

    if (key_length > EHFS_BT_KEY_MAX || value_length > EHFS_BT_VALUE_MAX ||
        key_length + value_length + REC_OVERHEAD > HALF)
        return EHFS_EINVAL;
    r = read_header(fs, tree);
    if (r != EHFS_OK)
        return r;

    uint8_t *hdr = fs->node[2];

    if (ehfs_le64(hdr + HDR_ROOT) == 0)
    {
        uint64_t n;

        r = alloc_node(fs, tree, &n);
        if (r != EHFS_OK)
            return r;
        node_init(fs->node[0], EHFS_NODE_LEAF, 1);
        node_insert(fs->node[0], 0, key, key_length, value, value_length);
        r = write_node(fs, tree, n, fs->node[0]);
        ehfs_put64(hdr + HDR_ROOT, n);
        ehfs_put32(hdr + HDR_DEPTH, 1);
        ehfs_put64(hdr + HDR_FIRST_LEAF, n);
        ehfs_put64(hdr + HDR_LAST_LEAF, n);
    }
    else
    {
        r = descend(fs, tree, key, key_length, &path);
        if (r != EHFS_OK)
            return r;
        lower_bound(fs->node[0], key, key_length, &exact);
        if (exact)
            return EHFS_EEXIST;
        r = insert_at(fs, tree, &path, (int)path.depth - 1, key, key_length, value, value_length);
    }
    if (r != EHFS_OK)
        return r;
    ehfs_put64(hdr + HDR_RECORDS, ehfs_le64(hdr + HDR_RECORDS) + 1);
    return write_header(fs, tree);
}

/* ---- Deleting ---- */

/* Take node n (in fs->node[0]) out of its level's chain. */
static int unlink_node(ehfs_t *fs, int tree, uint64_t n, bool leaf)
{
    uint8_t *node = fs->node[0];
    uint64_t prev = ehfs_le64(node + NODE_PREV), next = ehfs_le64(node + NODE_NEXT);
    int r;

    (void)n;
    if (prev != 0)
    {
        r = read_node(fs, tree, prev, fs->node[1]);
        if (r != EHFS_OK)
            return r;
        ehfs_put64(fs->node[1] + NODE_NEXT, next);
        r = write_node(fs, tree, prev, fs->node[1]);
        if (r != EHFS_OK)
            return r;
    }
    else if (leaf)
    {
        ehfs_put64(fs->node[2] + HDR_FIRST_LEAF, next);
    }
    if (next != 0)
    {
        r = read_node(fs, tree, next, fs->node[1]);
        if (r != EHFS_OK)
            return r;
        ehfs_put64(fs->node[1] + NODE_PREV, prev);
        r = write_node(fs, tree, next, fs->node[1]);
        if (r != EHFS_OK)
            return r;
    }
    else if (leaf)
    {
        ehfs_put64(fs->node[2] + HDR_LAST_LEAF, prev);
    }
    return EHFS_OK;
}

int ehfs_bt_delete(ehfs_t *fs, int tree, const uint8_t *key, uint32_t key_length)
{
    path_t path;
    bool exact;
    int r = read_header(fs, tree);

    if (r != EHFS_OK)
        return r;

    uint8_t *hdr = fs->node[2], *node = fs->node[0];

    if (ehfs_le64(hdr + HDR_ROOT) == 0)
        return EHFS_ENOENT;
    r = descend(fs, tree, key, key_length, &path);
    if (r != EHFS_OK)
        return r;

    uint32_t i = lower_bound(node, key, key_length, &exact);

    if (!exact)
        return EHFS_ENOENT;
    ehfs_put64(hdr + HDR_RECORDS, ehfs_le64(hdr + HDR_RECORDS) - 1);

    /* Up from the leaf: a node left empty goes, and its entry in the parent with it. */
    for (int level = (int)path.depth - 1;; level--)
    {
        uint64_t n = path.node[level];
        uint8_t bound[EHFS_BT_KEY_MAX];
        uint32_t bound_length;
        const uint8_t *k0 = key_of(node, 0, &bound_length);

        k_memcpy(bound, k0, bound_length);
        node_remove(node, i);
        if (count_of(node) > 0)
        {
            /*
             * An index node keeps its first key: it is the lower bound of everything below the node
             * (the parent's separator for it, or the empty key on the left edge).
             */
            if (i == 0 && node[NODE_KIND] == EHFS_NODE_INDEX)
            {
                uint8_t child[8];
                uint32_t v;

                k_memcpy(child, value_of(node, 0, &v), 8);
                node_remove(node, 0);
                node_insert(node, 0, bound, bound_length, child, 8);
            }
            r = write_node(fs, tree, n, node);
            break;
        }
        r = unlink_node(fs, tree, n, node[NODE_KIND] == EHFS_NODE_LEAF);
        if (r != EHFS_OK)
            return r;
        free_node(fs, n);
        if (level == 0)
        {
            ehfs_put64(hdr + HDR_ROOT, 0);      /* the tree is empty */
            ehfs_put32(hdr + HDR_DEPTH, 0);
            ehfs_put64(hdr + HDR_FIRST_LEAF, 0);
            ehfs_put64(hdr + HDR_LAST_LEAF, 0);
            break;
        }
        r = read_node(fs, tree, path.node[level - 1], node);
        if (r != EHFS_OK)
            return r;
        i = path.index[level - 1];
    }
    if (r != EHFS_OK)
        return r;

    /* A root index node with one child: the child becomes the root. */
    while (ehfs_le32(hdr + HDR_DEPTH) > 1)
    {
        uint64_t root = ehfs_le64(hdr + HDR_ROOT);

        r = read_node(fs, tree, root, node);
        if (r != EHFS_OK)
            return r;
        if (count_of(node) != 1)
            break;
        ehfs_put64(hdr + HDR_ROOT, child_of(node, 0));
        ehfs_put32(hdr + HDR_DEPTH, ehfs_le32(hdr + HDR_DEPTH) - 1);
        free_node(fs, root);
    }
    return write_header(fs, tree);
}

int ehfs_bt_replace(ehfs_t *fs, int tree, const uint8_t *key, uint32_t key_length, const void *value,
                    uint32_t value_length)
{
    path_t path;
    bool exact;
    int r = read_header(fs, tree);

    if (r != EHFS_OK)
        return r;
    if (ehfs_le64(fs->node[2] + HDR_ROOT) == 0)
        return EHFS_ENOENT;
    r = descend(fs, tree, key, key_length, &path);
    if (r != EHFS_OK)
        return r;

    uint32_t i = lower_bound(fs->node[0], key, key_length, &exact), length;

    if (!exact)
        return EHFS_ENOENT;

    uint8_t *v = (uint8_t *)value_of(fs->node[0], i, &length);

    if (length == value_length)
    {
        k_memcpy(v, value, value_length);
        return write_node(fs, tree, path.node[path.depth - 1], fs->node[0]);
    }
    r = ehfs_bt_delete(fs, tree, key, key_length);
    if (r == EHFS_OK)
        r = ehfs_bt_insert(fs, tree, key, key_length, value, value_length);
    return r;
}

/* ---- Walking ---- */

int ehfs_bt_walk(ehfs_t *fs, int tree, const uint8_t *start, uint32_t start_length, ehfs_bt_visit_t visit,
                 void *ctx)
{
    path_t path;
    bool exact;
    int r = read_header(fs, tree);

    if (r != EHFS_OK)
        return r;
    if (ehfs_le64(fs->node[2] + HDR_ROOT) == 0)
        return EHFS_OK;
    r = descend(fs, tree, start, start_length, &path);
    if (r != EHFS_OK)
        return r;

    uint8_t *node = fs->node[0];
    uint32_t i = lower_bound(node, start, start_length, &exact);

    for (;;)
    {
        for (; i < count_of(node); i++)
        {
            uint32_t k, v;
            const uint8_t *kp = key_of(node, i, &k);
            const uint8_t *vp = value_of(node, i, &v);
            int stop = visit(ctx, kp, k, vp, v);

            if (stop != 0)
                return stop;
        }

        uint64_t next = ehfs_le64(node + NODE_NEXT);

        if (next == 0)
            return EHFS_OK;
        r = read_node(fs, tree, next, node);
        if (r != EHFS_OK)
            return r;
        i = 0;
    }
}

/* ---- Checking ---- */

static void problem(ehfs_check_t *report, const char *what)
{
    if (report->errors++ == 0)
    {
        uint32_t i = 0;

        while (what[i] != '\0' && i + 1 < sizeof report->first)
        {
            report->first[i] = what[i];
            i++;
        }
        report->first[i] = '\0';
    }
}

int ehfs_bt_check(ehfs_t *fs, int tree, ehfs_check_t *report)
{
    uint8_t *hdr = fs->node[2], *node = fs->node[0], *seen = fs->node[3];
    uint8_t last_key[EHFS_BT_KEY_MAX];
    uint32_t last_length = 0;

    if (read_header(fs, tree) != EHFS_OK)
    {
        problem(report, "a B-tree's header node cannot be read");
        return EHFS_OK;
    }

    uint64_t nodes = ehfs_le64(hdr + HDR_NODES), root = ehfs_le64(hdr + HDR_ROOT);
    uint32_t depth = ehfs_le32(hdr + HDR_DEPTH);
    uint64_t records = 0, used = 1;

    k_memset(seen, 0, EHFS_NODE_SIZE);
    seen[0] = 1;
    if ((root == 0) != (depth == 0) || depth > MAX_DEPTH || root >= nodes)
    {
        problem(report, "a B-tree's root and depth do not match");
        return EHFS_OK;
    }

    /* Level by level from the root: the first node of each level is the first child of the one above. */
    uint64_t first = root;

    for (uint32_t level = 0; level < depth && first != 0; level++)
    {
        uint32_t height = depth - level;
        bool leaf = height == 1;
        uint64_t n = first, prev = 0, expected_child = 0, next_first = 0;
        bool have_key = false;

        if (leaf && ehfs_le64(hdr + HDR_FIRST_LEAF) != first)
            problem(report, "a B-tree's first leaf is wrong");
        while (n != 0)
        {
            if (n >= nodes || (seen[n / 8] & (1u << (n % 8))))
            {
                problem(report, "a B-tree's nodes loop or point outside it");
                return EHFS_OK;
            }
            seen[n / 8] |= (uint8_t)(1u << (n % 8));
            used++;
            if (read_node(fs, tree, n, node) != EHFS_OK)
            {
                problem(report, "a B-tree node is damaged");
                return EHFS_OK;
            }
            if (node[NODE_HEIGHT] != height || (node[NODE_KIND] == EHFS_NODE_LEAF) != leaf || count_of(node) == 0)
                problem(report, "a B-tree node has the wrong kind, height or no records");
            if (ehfs_le64(node + NODE_PREV) != prev)
                problem(report, "a B-tree node's previous link is wrong");

            uint32_t used_bytes = NODE_DATA;

            for (uint32_t i = 0; i < count_of(node); i++)
            {
                uint32_t k;
                const uint8_t *kp = key_of(node, i, &k);

                used_bytes += size_of(node, i);
                if (k > EHFS_BT_KEY_MAX || (have_key && compare(last_key, last_length, kp, k) >= 0))
                    problem(report, "a B-tree's keys are not in order");
                k_memcpy(last_key, kp, k > EHFS_BT_KEY_MAX ? EHFS_BT_KEY_MAX : k);
                last_length = k;
                have_key = true;
                if (!leaf)
                {
                    uint32_t vl;

                    value_of(node, i, &vl);
                    if (vl != 8)
                    {
                        problem(report, "a B-tree index record is not a node number");
                        return EHFS_OK;
                    }

                    uint64_t c = child_of(node, i);

                    if (expected_child == 0 && next_first == 0)
                        next_first = c;
                    else if (c != expected_child)
                        problem(report, "a B-tree's children are not the next level's chain");
                    /* The child's chain successor, read through the spare buffer. */
                    if (read_node(fs, tree, c, fs->node[1]) != EHFS_OK)
                    {
                        problem(report, "a B-tree node is damaged");
                        return EHFS_OK;
                    }

                    uint32_t ck;
                    const uint8_t *ckp = key_of(fs->node[1], 0, &ck);

                    if (count_of(fs->node[1]) > 0 && compare(ckp, ck, kp, k) < 0)
                        problem(report, "a B-tree child holds keys below its separator");
                    expected_child = ehfs_le64(fs->node[1] + NODE_NEXT);
                }
            }
            if (used_bytes != ehfs_le16(node + NODE_FREE))
                problem(report, "a B-tree node's free space is wrong");
            if (leaf)
                records += count_of(node);
            prev = n;
            n = ehfs_le64(node + NODE_NEXT);
        }
        if (!leaf && expected_child != 0)
            problem(report, "a B-tree level has nodes no parent points to");
        if (leaf && ehfs_le64(hdr + HDR_LAST_LEAF) != prev)
            problem(report, "a B-tree's last leaf is wrong");
        first = leaf ? 0 : next_first;
    }

    if (records != ehfs_le64(hdr + HDR_RECORDS))
        problem(report, "a B-tree's record count is wrong");
    for (uint64_t i = 0; i < nodes; i++)
    {
        bool mapped = (hdr[HDR_MAP + i / 8] >> (i % 8)) & 1;
        bool reached = (seen[i / 8] >> (i % 8)) & 1;

        if (mapped != reached)
        {
            problem(report, mapped ? "a B-tree node is marked in use but not in the tree"
                                   : "a B-tree node in the tree is marked free");
            break;
        }
    }
    if (nodes - used != ehfs_le64(hdr + HDR_FREE_NODES))
        problem(report, "a B-tree's free node count is wrong");
    return EHFS_OK;
}
