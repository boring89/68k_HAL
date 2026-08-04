/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "parser.h"

void shell_parse(
    char *input,
    shell_command_t *cmd
)
{
    cmd->argc = 0;

    for (int i = 0; i < SHELL_MAX_ARGS; i++)
    {
        cmd->argv[i] = 0;
    }

    while (*input)
    {
        while (*input == ' ')
            input++;

        if (*input == 0)
            break;

        if (cmd->argc >= SHELL_MAX_ARGS)
            break;

        cmd->argv[cmd->argc++] = input;


        while (*input &&
               *input != ' ')
        {
            input++;
        }


        if (*input)
        {
            *input = 0;
            input++;
        }
    }
}