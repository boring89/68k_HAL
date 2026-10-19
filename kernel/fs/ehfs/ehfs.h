/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_EHFS_EHFS_H
#define KERNEL_FS_EHFS_EHFS_H

#include <stdbool.h>
#include <stdint.h>

#include "format.h"
#include "kernel/block/block.h"

/*
 * eHFS, the 68k_HAL file system (docs/ehfs.md). Freestanding: the kernel and
 * the Linux tools (tools/ehfs/) use the same code. A volume lives on a block
 * device with 512-byte sectors, from start_sector on, in 4 KiB blocks.
 *
 * Every change to metadata (superblock, bitmap, B-tree nodes) belongs to a
 * transaction: ehfs_begin, the changes, ehfs_commit. The changed blocks are
 * kept in memory, then written to the journal with a commit block, then to
 * their places; a crash before the commit block is complete loses the whole
 * transaction, a crash after it is repaired at the next mount by writing the
 * blocks again. File data is written straight to its blocks before the
 * transaction that gives the blocks to the file commits (ordered mode).
 */

#define EHFS_TXN_BLOCKS 256u        /* metadata blocks one transaction may change (at most EHFS_TXN_MAX) */
#define EHFS_NODE_BUFFERS 4u        /* B-tree nodes worked on at the same time */

/* Results (0 = success) */
#define EHFS_OK 0
#define EHFS_EIO (-1)               /* the device failed */
#define EHFS_ECORRUPT (-2)          /* something on the disk is not what eHFS writes */
#define EHFS_ENOSPC (-3)            /* no free blocks (or B-tree nodes) */
#define EHFS_EINVAL (-4)            /* bad argument */
#define EHFS_EROFS (-5)             /* mounted read-only */
#define EHFS_ETXN (-6)              /* the transaction would be too large */
#define EHFS_ENOENT (-7)
#define EHFS_EEXIST (-8)
#define EHFS_ENOTDIR (-9)
#define EHFS_EISDIR (-10)
#define EHFS_ENOTEMPTY (-11)
#define EHFS_ENAME (-12)            /* not a valid name */
#define EHFS_EUNSUPPORTED (-13)     /* a feature this code does not know (incompat flags, block size) */

typedef struct
{
    block_device_t *dev;
    uint64_t start;                 /* the volume's first sector on dev */
    uint64_t blocks;                /* blocks in the volume */
    bool read_only;
    uint8_t sb[EHFS_SB_SIZE];       /* the superblock as it is now (changed inside transactions) */
    uint64_t alloc_hint;            /* where to start looking for free blocks */

    /* the transaction being built */
    bool in_txn;
    bool sb_dirty;
    uint32_t txn_count;
    uint64_t txn_target[EHFS_TXN_BLOCKS];
    uint8_t txn_data[EHFS_TXN_BLOCKS][EHFS_BLOCK_SIZE];
    uint64_t journal_sequence;      /* the last transaction written home */

    /* for tests: stop writing to the device after this many block writes (-1 = never) */
    int64_t crash_after;

    uint8_t block[EHFS_BLOCK_SIZE]; /* scratch */
    uint8_t node[EHFS_NODE_BUFFERS][EHFS_NODE_SIZE];
} ehfs_t;

/* Options of ehfs_format. */
typedef struct
{
    const char *name;               /* the volume's name, UTF-8 */
    uint8_t uuid[16];
    uint64_t now;                   /* nanoseconds since 1970 (UTC) */
    uint64_t journal_blocks;        /* 0 = the default (1/256 of the volume, 256..16384 blocks) */
} ehfs_format_options_t;

/* Make an empty eHFS volume of `sectors` sectors at start_sector. fs is used as working memory. */
int ehfs_format(ehfs_t *fs, block_device_t *dev, uint64_t start_sector, uint64_t sectors,
                const ehfs_format_options_t *options);

/*
 * Mount the volume: check the superblock, replay a committed transaction left
 * in the journal (unless read_only), and for writing mark it as mounted.
 */
int ehfs_mount(ehfs_t *fs, block_device_t *dev, uint64_t start_sector, bool read_only);

/* Mark the volume clean (when mounted for writing). */
int ehfs_unmount(ehfs_t *fs);

/* ---- Transactions and blocks (used by the rest of eHFS) ---- */
int ehfs_begin(ehfs_t *fs);
int ehfs_commit(ehfs_t *fs);
void ehfs_abort(ehfs_t *fs);    /* forget the changes of the transaction (the superblock is read back) */

/* Read block n (the transaction's copy when it changed it). */
int ehfs_read_block(ehfs_t *fs, uint64_t n, void *buffer);
/* Change metadata block n inside the transaction. */
int ehfs_write_meta(ehfs_t *fs, uint64_t n, const void *buffer);
/* Write a data block straight to the device (file contents). */
int ehfs_write_data(ehfs_t *fs, uint64_t n, const void *buffer);

/* ---- Superblock fields ---- */
uint64_t ehfs_sb_get64(const ehfs_t *fs, uint32_t offset);
void ehfs_sb_set64(ehfs_t *fs, uint32_t offset, uint64_t value);
uint32_t ehfs_sb_get32(const ehfs_t *fs, uint32_t offset);
void ehfs_sb_set32(ehfs_t *fs, uint32_t offset, uint32_t value);
/* A special file's fork descriptor (EHFS_FORK_SIZE bytes) inside the superblock. */
uint8_t *ehfs_sb_fork(ehfs_t *fs, int which);

/* ---- Blocks ---- */
/*
 * Allocate up to `want` free blocks in one run (at least 1), starting the
 * search at `near`. *start and *count tell what was taken.
 */
int ehfs_alloc(ehfs_t *fs, uint64_t near, uint64_t want, uint64_t *start, uint64_t *count);
int ehfs_free(ehfs_t *fs, uint64_t start, uint64_t count);
/* The device block of logical block index of a fork that has only its 8 inline extents. */
int ehfs_fork_inline_block(const uint8_t *fork, uint64_t index, uint64_t *block);
/* Whether block n is marked in use (for fsck). */
int ehfs_block_used(ehfs_t *fs, uint64_t n, bool *used);

/* ---- B-tree nodes ---- */
/* Store a node's CRC32C; whether a node's CRC32C is right. */
void ehfs_node_seal(uint8_t *node);
bool ehfs_node_intact(const uint8_t *node);
/* The header node (node 0) of an empty tree of `nodes` nodes. */
void ehfs_tree_header(uint8_t *node, uint64_t nodes);

/* ---- Checking ---- */
typedef struct
{
    uint32_t errors;
    char first[128];                /* a description of the first error */
} ehfs_check_t;

/* Check the volume's structures (fsck). Returns 0 if they are consistent. */
int ehfs_check(ehfs_t *fs, ehfs_check_t *report);

/* Little-endian helpers (also for the rest of eHFS). */
uint64_t ehfs_le64(const uint8_t *p);
uint32_t ehfs_le32(const uint8_t *p);
uint16_t ehfs_le16(const uint8_t *p);
void ehfs_put64(uint8_t *p, uint64_t v);
void ehfs_put32(uint8_t *p, uint32_t v);
void ehfs_put16(uint8_t *p, uint16_t v);

#endif
