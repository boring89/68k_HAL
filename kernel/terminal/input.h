/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_TERMINAL_INPUT_H
#define KERNEL_TERMINAL_INPUT_H

#include <stddef.h>

/*
 * The line editor of the shell: the keys typed (as the bytes a terminal
 * sends: keyboard_key_sequence) build a line that is handed to the submit
 * callback on Enter.
 *
 *   Left / Right, Home / End   move the cursor      (also Ctrl-A, Ctrl-E)
 *   Backspace, Delete          erase before / at the cursor
 *   Ctrl-U / Ctrl-K / Ctrl-W   erase to the start / to the end / the word before
 *   Up / Down                  earlier and later lines of the history (16 lines)
 *   Tab                        complete the word at the cursor (see below)
 *   Ctrl-C                     drop the line and start a new one
 *   Ctrl-L                     clear the screen and show the line again
 *
 * Without a display (terminal.h) only typing and Backspace at the end of
 * the line work: the editor cannot move the cursor there.
 */

typedef void (*terminal_submit_callback_t)(
    const char *
);

#define TERMINAL_INPUT_SIZE 128

/* Words the completer offers: at most this many, each shorter than this. */
#define TERMINAL_COMPLETION_MAX 32
#define TERMINAL_COMPLETION_LENGTH 64

typedef struct
{
    size_t start; /* the word to replace begins here (it ends at the cursor) */
    int count;
    /* The whole words (a directory ends with '/'), not only what is missing. */
    char item[TERMINAL_COMPLETION_MAX][TERMINAL_COMPLETION_LENGTH];
} terminal_completion_t;

/*
 * Offer the words that complete the text before the cursor: fill *completion
 * (start, count, item) for line[0 .. cursor). Called when Tab is pressed.
 */
typedef void (*terminal_completer_t)(
    const char *line,
    size_t cursor,
    terminal_completion_t *completion
);

void terminal_input_init(void);

void terminal_input_set_callback(terminal_submit_callback_t callback);

/* The completer for Tab; NULL = Tab does nothing. */
void terminal_input_set_completer(terminal_completer_t completer);

/* What the shell prints before the line: shown again after a list of completions and Ctrl-L. */
void terminal_input_set_prompt(const char *prompt);

void terminal_input_submit(void);

/* One byte of keyboard input: a character, a control character, or part of an escape sequence. */
void terminal_input_char(char c);

const char *terminal_input_get(void);

void terminal_input_clear(void);

#endif
