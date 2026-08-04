/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_SHELL_PARSER_H
#define KERNEL_SHELL_PARSER_H

#define SHELL_MAX_ARGS 16

typedef struct
{
    int argc;

    char *argv[SHELL_MAX_ARGS];

} shell_command_t;


void shell_parse(
    char *input,
    shell_command_t *cmd
);


#endif
