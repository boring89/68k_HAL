/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_EHFS_FORMAT_H
#define KERNEL_FS_EHFS_FORMAT_H

#include <stdint.h>

/*
 * The eHFS on-disk format (docs/ehfs.md). Little-endian, except that the
 * integers inside B-tree keys are big-endian so that every tree orders its
 * keys by plain byte comparison.
 */

#define EHFS_BLOCK_SIZE 4096u
#define EHFS_SECTORS_PER_BLOCK (EHFS_BLOCK_SIZE / 512u)

/* ---- Superblock: 3072 bytes at byte 1024 of block 0; a copy in the last block ---- */
#define EHFS_SB_OFFSET 1024u
#define EHFS_SB_SIZE 3072u
#define EHFS_MAGIC "eHFS\0\r\n\x1a"
#define EHFS_VERSION 1u

#define SB_MAGIC 0
#define SB_VERSION 8
#define SB_BLOCK_SIZE 12
#define SB_BLOCK_COUNT 16
#define SB_FREE_BLOCKS 24
#define SB_NEXT_CNID 32
#define SB_FILE_COUNT 40
#define SB_FOLDER_COUNT 48
#define SB_COMPAT 56
#define SB_RO_COMPAT 60
#define SB_INCOMPAT 64
#define SB_STATE 68
#define SB_UUID 72
#define SB_CREATED 88
#define SB_MODIFIED 96
#define SB_CHECKED 104
#define SB_MOUNTED 112
#define SB_FORKS 120               /* EHFS_SPECIAL_FORKS fork descriptors */
#define SB_NAME 984                /* length byte, then up to 255 bytes of UTF-8 */
#define SB_UNICODE 1240            /* the Unicode version of the case table (E2), e.g. 0x0F0000 */
#define SB_NODE_SIZE 1244          /* bytes per B-tree node */
#define SB_CRC 3068                /* CRC32C of bytes 0..3067 */

#define EHFS_STATE_CLEAN 0u
#define EHFS_STATE_MOUNTED 1u      /* mounted for writing (not cleanly unmounted if found at mount) */
#define EHFS_STATE_ERRORS 2u

/* The special files, in the superblock's order. */
enum
{
    EHFS_FORK_BITMAP,
    EHFS_FORK_CATALOG,
    EHFS_FORK_EXTENTS,
    EHFS_FORK_ATTRIBUTES,
    EHFS_FORK_JOURNAL,
    EHFS_FORK_BAD_BLOCKS,
    EHFS_SPECIAL_FORKS
};

/* ---- Fork descriptor: 144 bytes ---- */
#define EHFS_FORK_SIZE 144u
#define EHFS_INLINE_EXTENTS 8u
#define FORK_LENGTH 0              /* bytes */
#define FORK_BLOCKS 8              /* blocks allocated */
#define FORK_EXTENT 16             /* 8 x (start u64, count u64) */

/* ---- CNIDs ---- */
#define EHFS_CNID_ROOT_PARENT 1u
#define EHFS_CNID_ROOT 2u
#define EHFS_CNID_EXTENTS 3u
#define EHFS_CNID_CATALOG 4u
#define EHFS_CNID_BITMAP 5u
#define EHFS_CNID_ATTRIBUTES 6u
#define EHFS_CNID_JOURNAL 7u
#define EHFS_CNID_BAD_BLOCKS 8u
#define EHFS_CNID_FIRST 16u
#define EHFS_CNID_MAC_MAX 0xFFFFFFFFull     /* the Mac sees 32-bit IDs (E5) */

/* ---- B-tree node ---- */
#define EHFS_NODE_SIZE 16384u       /* E3: 4 blocks */
#define EHFS_NODE_BLOCKS (EHFS_NODE_SIZE / EHFS_BLOCK_SIZE)
#define NODE_CRC 0                  /* CRC32C of bytes 4..end */
#define NODE_KIND 4
#define NODE_HEIGHT 5               /* 1 = leaf */
#define NODE_COUNT 6
#define NODE_NEXT 8
#define NODE_PREV 16
#define NODE_FREE 24                /* where free space starts (after the record data) */
#define NODE_DATA 32                /* records grow up from here; their offsets (u16) grow down from the end */

#define EHFS_NODE_HEADER 1u
#define EHFS_NODE_INDEX 2u
#define EHFS_NODE_LEAF 3u

/* Node 0, the header node */
#define HDR_ROOT 32                 /* node number, 0 = empty tree */
#define HDR_DEPTH 40
#define HDR_NODES 48                /* nodes in the tree's file */
#define HDR_FREE_NODES 56
#define HDR_RECORDS 64              /* leaf records */
#define HDR_FIRST_LEAF 72
#define HDR_LAST_LEAF 80
#define HDR_NODE_SIZE 88
#define HDR_MAP 96                  /* one bit per node: in use */
#define EHFS_MAX_NODES ((EHFS_NODE_SIZE - HDR_MAP) * 8u)

/* ---- Catalog records (values; keys = BE64 parent + comparison form of the name) ---- */
#define EHFS_REC_FOLDER 1u
#define EHFS_REC_FILE 2u
#define EHFS_REC_FOLDER_THREAD 3u
#define EHFS_REC_FILE_THREAD 4u

#define REC_TYPE 0                  /* u16 */
#define REC_FLAGS 2                 /* u16: bit 0 locked */
#define REC_CNID 4                  /* u64 */
#define REC_VALENCE 12              /* folders: u64 items inside */
#define REC_TIMES 20                /* u64 x 5: created, modified, attributes changed, accessed, backed up (ns) */
#define REC_FINDER 60               /* 32 bytes: FInfo+FXInfo or DInfo+DXInfo */
#define REC_FOLDER_NAME 92          /* folders: u16 length, then the original name (UTF-8) */
#define REC_DATA_FORK 92            /* files: fork descriptors */
#define REC_RSRC_FORK (92 + 144)
#define REC_FILE_NAME (92 + 288)    /* files: u16 length, then the name */
#define THREAD_PARENT 4             /* threads: u64 parent, u16 length, name */
#define THREAD_NAME 12

#define EHFS_TIME_COUNT 5

/* ---- Journal (CNID 7): header block, then one transaction at a time ---- */
#define EHFS_JOURNAL_MAGIC "eHFSjrnl"
#define EHFS_TXN_MAGIC "eHFStxn\0"
#define EHFS_COMMIT_MAGIC "eHFScmt\0"
#define JHDR_MAGIC 0
#define JHDR_SEQUENCE 8             /* u64: the last transaction written home */
#define JHDR_CRC 16
#define TXN_MAGIC 0
#define TXN_SEQUENCE 8
#define TXN_COUNT 16                /* u32 */
#define TXN_TARGETS 24              /* u64 x count */
#define EHFS_TXN_MAX ((EHFS_BLOCK_SIZE - TXN_TARGETS) / 8u)
#define COMMIT_MAGIC 0
#define COMMIT_SEQUENCE 8
#define COMMIT_COUNT 16
#define COMMIT_CRC 20               /* CRC32C of the descriptor block and every image */

#endif
