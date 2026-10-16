/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_PS2_KEYBOARD_KEYMAP_H
#define DRIVERS_PS2_KEYBOARD_KEYMAP_H

#include <stdint.h>

#include "kernel/keyboard/keycode.h"

keycode_t keyboard_scancode_to_keycode(uint8_t scancode);

/* The same for the keys that send 0xE0 first (arrows, Delete, right Ctrl/Alt...). */
keycode_t keyboard_extended_scancode_to_keycode(uint8_t scancode);

#endif