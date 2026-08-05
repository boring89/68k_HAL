/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "command.h"
#include "../terminal/terminal.h"

#include "../timer/pit.h"

#include "../lib/k_string.h"
#include "../lib/k_number.h"

#include "../memory/memory.h"

static void *last_alloc = 0;

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
static void cmd_memory(
    int argc,
    char **argv);
static void cmd_alloc(
    int argc,
    char **argv);
static void cmd_free(
    int argc,
    char **argv);
static void cmd_heap(
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
         "Display PIT ticks",
         cmd_ticks},
        {COMMAND_INFO,
         "info",
         "Display kernel information",
         cmd_info},
        {COMMAND_MEMORY,
         "memory",
         "Display heap usage",
         cmd_memory},
        {COMMAND_HEAP,
         "heap",
         "Display heap block information",
         cmd_heap},
        {COMMAND_ALLOC,
         "alloc",
         "Allocate memory",
         cmd_alloc},
        {COMMAND_FREE,
         "free",
         "Free up last allocation",
         cmd_free}};

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

void cmd_memory(
    int argc,
    char **argv)
{
    char buffer[32];

    terminal_newline();
    terminal_write("Heap Start: ");

    k_utoa(
        heap_get_start(),
        buffer);

    terminal_write_line(buffer);

    terminal_write("Heap Current: ");

    k_utoa(
        heap_get_current(),
        buffer);

    terminal_write_line(buffer);

    terminal_write("Reversed: ");

    k_utoa(
        heap_get_reserved(),
        buffer);

    terminal_write_line(buffer);

    terminal_write("Used: ");

    k_utoa(
        heap_get_used(),
        buffer);

    terminal_write_line(buffer);
    terminal_newline();
}

static void cmd_heap(
    int argc,
    char **argv)
{
    heap_block_info_t info;

    int i = 0;

    while (heap_get_block(i, &info) == 0)
    {
        char buffer[32];

        terminal_write("Block ");

        k_itoa(i, buffer);
        terminal_write(buffer);

        terminal_newline();

        terminal_write("Header: ");

        k_utoa_hex(
            info.header,
            buffer);

        terminal_write(buffer);

        terminal_newline();

        terminal_write("Address: ");

        k_utoa_hex(
            info.address,
            buffer);

        terminal_write(buffer);

        terminal_newline();

        terminal_write("Size: ");

        k_itoa(
            info.size,
            buffer);

        terminal_write(buffer);

        terminal_newline();

        terminal_write("Used: ");

        if (info.used)
            terminal_write("yes");
        else
            terminal_write("no");

        terminal_newline();
        terminal_newline();

        i++;
    }
}

static void cmd_alloc(
    int argc,
    char **argv)
{
    if (argc < 2)
    {
        terminal_write_line(
            "usage: alloc <size>");
        return;
    }

    int argv_buffer;
    k_atoi(argv[1],
           &argv_buffer);

    void *ptr =
        kmalloc(argv_buffer);

    char size_buffer[32];
    char addr_buffer[32];

    k_utoa(
        argv_buffer,
        size_buffer);

    k_utoa_ptr(
        (uint32_t)ptr,
        addr_buffer);

    terminal_write("Allocated ");
    terminal_write(size_buffer);

    terminal_write(" bytes at ");

    terminal_write_line(addr_buffer);
}

static void cmd_free(
    int argc,
    char **argv)
{
    if (argc < 2)
    {
        terminal_write("Usage: free <address>");
        terminal_newline();
        return;
    }

    uint32_t address =
        k_atoh(argv[1]);

    kfree((void *)address);

    terminal_write("Memory freed");
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
