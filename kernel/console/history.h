/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_CONSOLE_HISTORY_H
#define KERNEL_CONSOLE_HISTORY_H

#include <stddef.h>

/*
 * A copy of the last 16 KiB written to the console, so the boot messages can be read again
 * later (the `dmesg` command, the hardware diagnostics pages) after they scrolled off the
 * screen.
 */
#define CONSOLE_HISTORY_SIZE 16384

/* Start recording (registers a console sink). Returns 0, or -1 if the sink table is full. */
int console_history_start(void);

/* Add text directly (what the sink does; also for tests). */
void console_history_add(const char *text);

/* Number of bytes held now (at most CONSOLE_HISTORY_SIZE). */
size_t console_history_size(void);

/*
 * Copy the last `lines` lines (all of it if lines is 0 or larger than what is there) into
 * buffer (size bytes, NUL-terminated; the oldest part is dropped if it does not fit). Returns
 * the number of bytes written without the NUL.
 */
size_t console_history_last_lines(char *buffer, size_t size, unsigned int lines);

/* Forget everything (tests). */
void console_history_clear(void);

#endif
