/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "command.h"

#include <stddef.h>
#include <stdint.h>
#include "../terminal/terminal.h"

#include "../timer/pit.h"

#include "../lib/k_string.h"
#include "../lib/k_number.h"

#include "../memory/memory.h"

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

#define COMMAND_TABLE_SIZE \
    ((int)(sizeof(commands) / sizeof(commands[0])))

/* Parse a positive decimal size; rejects signs, junk and overflow. */
static int parse_size(
    const char *str,
    size_t *size)
{
    size_t value = 0;

    if (*str == '\0')
        return -1;

    while (*str)
    {
        if (*str < '0' || *str > '9')
            return -1;

        size_t digit = (size_t)(*str - '0');

        if (value > (SIZE_MAX - digit) / 10)
            return -1;

        value = value * 10 + digit;
        str++;
    }

    if (value == 0)
        return -1;

    *size = value;
    return 0;
}

static void cmd_help(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    terminal_write_line("Available command: \n");

    for (int i = 0; i < COMMAND_TABLE_SIZE; i++)
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
    (void)argc;
    (void)argv;

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

static void cmd_ticks(int argc, char **argv)
{
    (void)argc;
    (void)argv;

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
    (void)argc;
    (void)argv;

    terminal_write_line("68k_HAL Kernel");
    terminal_write_line("Architecture: x86");
    terminal_write_line("Mode: Protected Mode");
    terminal_write_line("Version: 0.0.1");
    terminal_newline();
}

static void cmd_memory(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    char buffer[32];

    terminal_newline();
    terminal_write("Kernel End: ");

    k_utoa(
        memory_get_kernel_end(),
        buffer);

    terminal_write_line(buffer);

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

    terminal_write("Reserved: ");

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
    (void)argc;
    (void)argv;

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

    size_t size;

    if (parse_size(argv[1], &size) != 0)
    {
        terminal_write_line(
            "usage: alloc <size>  (size: positive decimal bytes)");
        return;
    }

    void *ptr =
        kmalloc(size);

    if (ptr == 0)
    {
        terminal_write_line("Allocation failed");
        return;
    }

    char size_buffer[32];
    char addr_buffer[32];

    k_utoa(
        size,
        size_buffer);

    k_utoa_ptr(
        (uintptr_t)ptr,
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

    uintptr_t address =
        k_atoh(argv[1]);

    if (kfree((void *)address) != 0)
    {
        terminal_write("Warning: not an allocated block, heap unchanged");
        terminal_newline();
        return;
    }

    terminal_write("Memory freed");
    terminal_newline();
}

void shell_dispatch(shell_command_t *cmd)
{
    if (cmd->argc == 0)
        return;

    for (int i = 0; i < COMMAND_TABLE_SIZE; i++)
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
