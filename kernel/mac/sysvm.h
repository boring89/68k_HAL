/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MAC_SYSVM_H
#define KERNEL_MAC_SYSVM_H

#include "kernel/block/block.h"

/*
 * The Memory control panel keeps its "Virtual Memory On/Off" choice in the
 * System file, resource 'vm  ' 0: a 32-bit word, 1 = on, then the size of
 * the backing file and the name of its volume (found by experiment with
 * System 7.5.3; Inside Macintosh does not document it; docs/mac.md section
 * 14). This machine cannot run virtual memory: with it on, the Mac stops at
 * "Welcome to Macintosh" on every start. Because the disks keep the Mac's
 * changes, the choice is undone here before the Mac starts.
 */

/*
 * Find the System file of the HFS volume on disk (an Apple_HFS partition, or
 * a bare volume at block 0): the file of type 'zsys' in the blessed folder.
 * If its 'vm  ' 0 resource says "on", write 0 over that word, in place (only
 * the file's bytes change; no HFS structure does). Returns 1 if it was on and
 * is now off, 0 if there was nothing to do (no HFS volume, no System file,
 * no such resource, already off), -1 on a read or write error, a damaged
 * resource fork or a read-only disk that would need the change.
 */
int sysvm_turn_off(block_device_t *disk);

#endif
