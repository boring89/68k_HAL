/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_FS_EHFS_NAME_H
#define KERNEL_FS_EHFS_NAME_H

#include <stdbool.h>
#include <stdint.h>

#define EHFS_NAME_MAX 255u          /* bytes of UTF-8 */
#define EHFS_KEY_MAX 1020u          /* bytes of a comparison form (folding can make a name longer) */

/* A name may be used: 1 to EHFS_NAME_MAX bytes of valid UTF-8 without NUL or '/'. */
bool ehfs_name_valid(const uint8_t *name, uint32_t length);

/*
 * The comparison form of a name (NFC, then simple case folding) as UTF-8 in
 * out. Returns its length, or -1 if the name is not valid or out is too small.
 */
int ehfs_name_key(const uint8_t *name, uint32_t length, uint8_t *out, uint32_t out_size);

#endif
