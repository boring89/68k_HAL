/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_KEYBOARD_KEYMAP_H
#define KERNEL_KEYBOARD_KEYMAP_H

#include <stdint.h>

#include "keycode.h"

keycode_t keyboard_scancode_to_keycode(uint8_t scancode);

#endif