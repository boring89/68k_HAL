/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "command.h"
#include "../terminal/terminal.h"

#include "../timer/pit.h"

#include "../lib/k_string.h"
#include "../lib/k_number.h"

static void cmd_help(
    int argc,
    char **argv);

static void cmd_clear(
    int argc,
    char **argv);

static void cmd_echo(
    int argc,
    char **argv);

static void cmd_ticks(
    int argc,
    char **argv);
static void cmd_info(
    int argc,
    char **argv);

static shell_command_entry_t commands[] =
    {
        {COMMAND_HELP,
         "help",
         "List available commands",
         cmd_help},

        {COMMAND_CLEAR,
         "clear",
         "Clear terminal",
         cmd_clear},

        {COMMAND_ECHO,
         "echo",
         "Print text",
         cmd_echo},

        {COMMAND_TICKS,
         "ticks",
         "Show ticks",
         cmd_ticks},
        {COMMAND_INFO,
         "info",
         "Show kernel infomation",
         cmd_info}};

static void cmd_help(
    int argc,
    char **argv)
{
    terminal_write_line("Available command: \n");

    for (int i = 0; i < COMMAND_COUNT; i++)
    {
        terminal_write(commands[i].name);
        terminal_write(" - ");
        terminal_write_line(commands[i].description);
        terminal_newline();
    }
}

static void cmd_clear(
    int argc,
    char **argv)
{
    terminal_clear();
}

static void cmd_echo(
    int argc,
    char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        terminal_write(argv[i]);

        if (i + 1 < argc)
            terminal_put_char(' ');
    }

    terminal_newline();
}

void cmd_ticks(int argc, char **argv)
{
    char buffer[32];

    k_itoa(
        pit_get_ticks(),
        buffer);

    terminal_write_line(buffer);
}

static void cmd_info(
    int argc,
    char **argv)
{
    terminal_write_line("68k_HAL Kernel");
    terminal_write_line("Architecture: x86");
    terminal_write_line("Mode: Protected Mode");
    terminal_write_line("Version: 0.0.1");
    terminal_newline();
}

void shell_dispatch(shell_command_t *cmd)
{
    if (cmd->argc == 0)
        return;

    for (int i = 0; i < COMMAND_COUNT; i++)
    {
        if (k_strcmp(
                cmd->argv[0],
                commands[i].name) == 0)
        {
            commands[i].handler(
                cmd->argc,
                cmd->argv);

            return;
        }
    }

    terminal_write("Unknown command: ");
    terminal_write_line(cmd->argv[0]);
}
