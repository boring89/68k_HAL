/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_INPUT_LINUX_KEYS_H
#define KERNEL_INPUT_LINUX_KEYS_H

#include "kernel/keyboard/keycode.h"

/*
 * The number Linux gives a key in its input event interface (KEY_* in
 * linux/input-event-codes.h: KEY_ESC = 1, KEY_Q = 16, KEY_ENTER = 28,
 * KEY_UP = 103, ...), which programs written for /dev/input/event* expect.
 * Returns 0 for KEY_NONE and for anything unknown.
 */
int input_linux_key_code(keycode_t key);

#endif
