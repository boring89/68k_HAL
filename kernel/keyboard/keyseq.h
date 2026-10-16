/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_KEYBOARD_KEYSEQ_H
#define KERNEL_KEYBOARD_KEYSEQ_H

#include "keycode.h"
#include "state.h"

#define KEYBOARD_SEQUENCE_MAX 8

/*
 * The bytes a program reads when a key is pressed, as on a Linux
 * console / xterm:
 *
 *   letters, digits, punctuation   the character (Ctrl-letter: the control character)
 *   Enter '\r', Backspace 0x7F, Tab '\t', Esc 0x1B
 *   arrows        ESC [ A / B / C / D (up, down, right, left)
 *   Home, End     ESC [ H, ESC [ F
 *   Insert, Delete, Page Up, Page Down   ESC [ 2 ~, 3 ~, 5 ~, 6 ~
 *   F1-F4         ESC O P / Q / R / S
 *   F5-F12        ESC [ 15 ~, 17 ~, 18 ~, 19 ~, 20 ~, 21 ~, 23 ~, 24 ~
 *   Alt + key     ESC followed by the bytes of the key
 *
 * Writes them to out and returns how many (0 for keys that send nothing,
 * such as Shift or Caps Lock). out has room for KEYBOARD_SEQUENCE_MAX.
 */
int keyboard_key_sequence(keycode_t key, const keyboard_state_t *state, char *out);

#endif
