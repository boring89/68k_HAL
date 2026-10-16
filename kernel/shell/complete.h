/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_SHELL_COMPLETE_H
#define KERNEL_SHELL_COMPLETE_H

#include "kernel/terminal/input.h"

/*
 * Tab completion for the shell line (the completer of terminal_input): the
 * first word completes to a built-in command or a program in /bin; any
 * other word to a file or directory name of the file tree, relative to the
 * current directory (a word with a slash: in the directory before the last
 * slash). Names that start with a dot are offered when a dot is typed.
 */
/* Where the built-in command names come from (count, then name by index). */
void shell_complete_set_commands(int (*count)(void), const char *(*name)(int index));

void shell_complete(const char *line, size_t cursor, terminal_completion_t *completion);

#endif
