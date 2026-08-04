/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "shell.h"

#include "../terminal/terminal.h"

#include "parser.h"
#include "command.h"

#include "../lib/k_string.h"

void shell_prompt(void)
{
    terminal_write("HAL> ");
}

void shell_init(void)
{
    shell_prompt();
}

void shell_execute(const char *input)
{
    char buffer[128];

    k_strcpy(buffer, input);

    shell_command_t cmd;

    shell_parse(
        buffer,
        &cmd
    );

    shell_dispatch(&cmd);

    shell_prompt();
}