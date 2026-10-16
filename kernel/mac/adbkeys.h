/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MAC_ADBKEYS_H
#define KERNEL_MAC_ADBKEYS_H

#include "kernel/keyboard/keycode.h"

/*
 * The ADB key code (0x00-0x7F) a Macintosh keyboard sends for a key of ours,
 * or -1 if the Mac has no such key here. Key codes of the US layout: the
 * table in Inside Macintosh: Text, "Virtual key codes".
 */
int mac_adb_key_code(keycode_t key);

#endif
