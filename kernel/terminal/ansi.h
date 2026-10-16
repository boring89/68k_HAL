/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_TERMINAL_ANSI_H
#define KERNEL_TERMINAL_ANSI_H

/*
 * Output for programs: the terminal plus the common ANSI escape
 * sequences (ECMA-48), the subset that curses-style programs and colour
 * output use:
 *
 *   ESC [ n A/B/C/D      cursor up/down/right/left          ESC [ r;c H or f   move to row r, column c (1-based)
 *   ESC [ n E/F          start of the next/previous line    ESC [ c G          move to column c
 *   ESC [ r d            move to row r                      ESC [ s / u        save / restore the cursor
 *   ESC [ 0/1/2 J        erase screen (end / start / all)   ESC [ 0/1/2 K      erase line (end / start / all)
 *   ESC [ ... m          colours: 0 reset, 1 bold, 7 reverse, 30-37 / 90-97 foreground,
 *                        40-47 / 100-107 background, 39 / 49 default
 *
 * Also '\r' (to the start of the line) and '\t' (to the next multiple of 8).
 * Other sequences are read and ignored. Every byte also goes to the
 * mirror (the serial port) unchanged, so a terminal program on the other
 * side shows the same thing.
 */

/* Interpret one byte of program output. */
void ansi_put_char(char c);

/* Back to the default colours and drop a half-read sequence. */
void ansi_reset(void);

#endif
