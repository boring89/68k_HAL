/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_USER_INPUTDEV_H
#define KERNEL_USER_INPUTDEV_H

#include "kernel/vfs/vfs.h"

/*
 * /dev/input/event0: keyboard and mouse as Linux input events
 * (linux/input.h), for programs that want key releases and mouse movement
 * and not typed characters. read() gives whole events, 16 bytes each:
 *
 *   uint32_t seconds, microseconds;  uint16_t type, code;  int32_t value;
 *
 *   EV_KEY (1): code = KEY_* (input_linux_key_code) or BTN_LEFT/RIGHT/MIDDLE
 *               (0x110..0x112), value 1 pressed, 0 released
 *   EV_REL (2): code REL_X (0) / REL_Y (1), value = movement (+y down)
 *   EV_SYN (0): ends a group of events that belong together
 *
 * It waits for an event unless the file is O_NONBLOCK (then -EAGAIN); poll()
 * says when one is there. There is no key repeat.
 */

extern const vfs_file_ops_t inputdev_ops;

/* Forget events that arrived before a program opened the device. */
void inputdev_flush(void);

/* True while some program has the device open (then the keyboard events are its). */
int inputdev_is_open(void);

#endif
