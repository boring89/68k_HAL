/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_USER_CONSOLE_H
#define KERNEL_USER_CONSOLE_H

#include "kernel/vfs/vfs.h"

/*
 * The device directory /dev: console (and tty, the same), null, zero,
 * urandom. The console writes to the screen and the serial port
 * (ANSI escape sequences are understood, see kernel/terminal/ansi.h);
 * reads come from the keyboard, by default one line at a time with echo
 * and backspace. The ioctls TCGETS/TCSETS (termios: raw mode, no echo,
 * VMIN/VTIME, Ctrl-C), TIOCGWINSZ and FIONREAD are supported.
 */

/* Default termios, no pending input, default colours. Called before and after each program. */
void user_console_reset(void);

/* A file system with the devices, for mounting at /dev. NULL if out of memory. */
vfs_fs_t *devfs_create(void);

/*
 * Tests: copy everything the programs write to the console into buffer
 * (size bytes, always NUL terminated; the rest is dropped) until
 * user_console_capture_stop(). Output still goes to the screen too.
 */
void user_console_capture_start(char *buffer, size_t size);
size_t user_console_capture_stop(void);

/* Open the console as a stand-alone file (for descriptors 0, 1 and 2). */
vfs_file_t *console_file_open(uint32_t flags);

#endif
