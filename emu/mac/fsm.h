/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef EMU_MAC_FSM_H
#define EMU_MAC_FSM_H

#include <stdbool.h>
#include <stdint.h>

/*
 * An external file system for the Mac OS (System 7.5's File System Manager,
 * docs/fsm.md): a volume whose files come from the host through the
 * callbacks below, not from blocks of a disk. The 68k half is emu/mac/fsm.s,
 * the File Manager's side is emu/mac/fsm.c. Read-only for now.
 */

#define MAC_FS_ROOT_PARENT 1u   /* the root folder's parent ID */
#define MAC_FS_ROOT_ID 2u       /* the root folder's ID */
#define MAC_FS_FIRST_ID 16u     /* IDs below this are reserved (fsUsrCNID) */
#define MAC_FS_NAME_MAX 31u

/* One file or folder as the Mac sees it. */
typedef struct
{
    uint32_t id, parent;
    bool is_dir;
    uint8_t name[MAC_FS_NAME_MAX + 1];  /* Pascal string, Mac Roman */
    uint32_t data_length, rsrc_length;  /* files */
    uint32_t valence;                   /* folders: how many items inside */
    uint8_t finfo[16];                  /* FInfo (files) or DInfo (folders) */
    uint8_t fxinfo[16];                 /* FXInfo or DXInfo */
    uint32_t created, modified;         /* seconds since 1904 */
} mac_fs_item_t;

/*
 * The host's side. Every call returns 0 (or the byte count for read) on
 * success, -1 if there is no such item (or the read fails).
 */
typedef struct
{
    void *ctx;
    const char *volume_name;    /* Mac Roman, at most 27 characters */
    int (*get)(void *ctx, uint32_t id, mac_fs_item_t *item);
    /* The item called name (Mac Roman, length bytes; letters compared without case) in folder parent. */
    int (*lookup)(void *ctx, uint32_t parent, const uint8_t *name, uint32_t length, mac_fs_item_t *item);
    /* Item number index (from 1) of folder parent. */
    int (*child)(void *ctx, uint32_t parent, uint32_t index, mac_fs_item_t *item);
    int32_t (*read)(void *ctx, uint32_t id, bool resource_fork, uint32_t offset, void *buffer, uint32_t length);
} mac_fs_ops_t;

#define MAC_FSM_FRAMES 4        /* nested calls (the install's VolumeMount calls the HFS procedure) */
#define MAC_FSM_CALLS 32        /* trap calls one File Manager request may make */

/* One call of the 68k half in progress (fsm.c). */
typedef struct
{
    uint8_t kind;
    uint16_t select;
    uint32_t vcb, pb, globals;
    uint32_t count;             /* trap calls made so far */
    uint32_t next;              /* the next one while replaying */
    uint32_t d0[MAC_FSM_CALLS], a0[MAC_FSM_CALLS];
} mac_fsm_frame_t;

typedef struct
{
    const mac_fs_ops_t *ops;    /* NULL: no volume */
    bool install_pending;       /* inject the install once the Finder runs */
    bool mounted;
    uint32_t data;              /* the plug-in's block in the system heap (FSDRec, drive, scratch) */
    uint32_t vcb;
    int16_t drive, vrefnum;
    int16_t install_error;      /* the install's result: 0, an OSErr, or -1/-2 no FSM 1.2, -3 no memory */
    bool overflow;              /* calls nested deeper than MAC_FSM_FRAMES */
    uint32_t restore_addr, restore_value;   /* a long to put back after the call being made */
    uint32_t depth;
    mac_fsm_frame_t frame[MAC_FSM_FRAMES];
    uint32_t requests;          /* File Manager requests served */
    struct
    {
        uint16_t select;
        int16_t result;
        uint8_t permission;     /* ioPermssn (opens) */
    } log[64];                  /* the last requests, for diagnostics (index = request number % 64) */
} mac_fsm_t;

#endif
