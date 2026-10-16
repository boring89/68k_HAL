/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "keyseq.h"

#include "keychar.h"

#include <stddef.h>

/* Keys that send a fixed string (xterm's, which is also what the Linux console sends). */
static const struct
{
    keycode_t key;
    const char *bytes;
} sequences[] = {
    {KEY_UP, "\x1b[A"},
    {KEY_DOWN, "\x1b[B"},
    {KEY_RIGHT, "\x1b[C"},
    {KEY_LEFT, "\x1b[D"},
    {KEY_HOME, "\x1b[H"},
    {KEY_END, "\x1b[F"},
    {KEY_INSERT, "\x1b[2~"},
    {KEY_DELETE, "\x1b[3~"},
    {KEY_PAGE_UP, "\x1b[5~"},
    {KEY_PAGE_DOWN, "\x1b[6~"},
    {KEY_F1, "\x1bOP"},
    {KEY_F2, "\x1bOQ"},
    {KEY_F3, "\x1bOR"},
    {KEY_F4, "\x1bOS"},
    {KEY_F5, "\x1b[15~"},
    {KEY_F6, "\x1b[17~"},
    {KEY_F7, "\x1b[18~"},
    {KEY_F8, "\x1b[19~"},
    {KEY_F9, "\x1b[20~"},
    {KEY_F10, "\x1b[21~"},
    {KEY_F11, "\x1b[23~"},
    {KEY_F12, "\x1b[24~"},
};

/* The single byte of a key, or -1. */
static int key_byte(keycode_t key, const keyboard_state_t *state)
{
    bool ctrl = state->left_ctrl || state->right_ctrl;

    switch (key)
    {
    case KEY_ENTER:
        return '\r';

    case KEY_BACKSPACE:
        return 0x7F;

    case KEY_ESC:
        return 0x1B;

    default:
        break;
    }

    /* Ctrl with the keys that have no letter: Ctrl-[ is Escape, Ctrl-Space is NUL. */
    if (ctrl)
    {
        switch (key)
        {
        case KEY_LEFT_BRACKET:
            return 0x1B;

        case KEY_BACKSLASH:
            return 0x1C;

        case KEY_RIGHT_BRACKET:
            return 0x1D;

        case KEY_SPACE:
            return 0;

        default:
            break;
        }
    }

    char c = keyboard_keycode_to_ascii(key, state);

    /* keychar gives 0 for keys without a character; Ctrl-Space is the only NUL handled above. */
    return c != 0 ? (unsigned char)c : -1;
}

int keyboard_key_sequence(keycode_t key, const keyboard_state_t *state, char *out)
{
    int length = 0;
    const char *fixed = NULL;

    for (size_t i = 0; i < sizeof(sequences) / sizeof(sequences[0]); i++)
    {
        if (sequences[i].key == key)
            fixed = sequences[i].bytes;
    }

    int single = fixed == NULL ? key_byte(key, state) : -1;

    if (fixed == NULL && single < 0)
        return 0;

    /* Alt sends Escape first. */
    if ((state->left_alt || state->right_alt) && key != KEY_ESC)
        out[length++] = 0x1B;

    if (fixed != NULL)
    {
        while (*fixed != '\0')
            out[length++] = *fixed++;
    }
    else
    {
        out[length++] = (char)single;
    }

    return length;
}
