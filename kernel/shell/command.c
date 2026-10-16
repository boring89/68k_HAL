/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "command.h"

#include <stddef.h>
#include <stdint.h>
#include "kernel/terminal/terminal.h"

#include "hal/timer.h"

#include "kernel/lib/k_string.h"
#include "kernel/lib/k_number.h"

#include "kernel/m68k/demo.h"
#include "kernel/mac/machine.h"
#include "kernel/memory/memory.h"
#include "kernel/block/block.h"
#include "kernel/fs/volume.h"
#include "hal/boot.h"
#include "hal/clock.h"
#include "hal/cpu.h"

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
static void cmd_mmap(
    int argc,
    char **argv);
static void cmd_m68k(
    int argc,
    char **argv);
static void cmd_modules(
    int argc,
    char **argv);
static void cmd_date(
    int argc,
    char **argv);
static void cmd_disks(
    int argc,
    char **argv);
static void cmd_ls(
    int argc,
    char **argv);
static void cmd_cat(
    int argc,
    char **argv);
static void cmd_cpuinfo(
    int argc,
    char **argv);
static void cmd_mac(
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
         "Display system timer ticks",
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
         "Free the allocation at <address>",
         cmd_free},
        {COMMAND_MMAP,
         "mmap",
         "Display boot memory map",
         cmd_mmap},
        {COMMAND_M68K,
         "m68k",
         "Run a demo program on the 68k emulator",
         cmd_m68k},
        {COMMAND_MODULES,
         "modules",
         "List files loaded by the boot loader",
         cmd_modules},
        {COMMAND_DATE,
         "date",
         "Display the current date and time (UTC)",
         cmd_date},
        {COMMAND_DISKS,
         "disks",
         "List block devices",
         cmd_disks},
        {COMMAND_LS,
         "ls",
         "List a folder of the HFS volume (ls [path], folders separated by :)",
         cmd_ls},
        {COMMAND_CAT,
         "cat",
         "Show a text file of the HFS volume (cat <path>)",
         cmd_cat},
        {COMMAND_MAC,
         "mac",
         "Run the Macintosh IIci on boot modules mac_rom and mac_disk (` stops)",
         cmd_mac},
        {COMMAND_CPUINFO,
         "cpuinfo",
         "Display the CPU model and features",
         cmd_cpuinfo}};

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
        hal_timer_ticks(),
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

/* Write a 32-bit value as exactly 8 hex digits. */
static void write_hex32(uint32_t value)
{
    const char digits[] = "0123456789ABCDEF";
    char buffer[9];

    for (int i = 7; i >= 0; i--)
    {
        buffer[i] = digits[value & 0xF];
        value >>= 4;
    }

    buffer[8] = '\0';
    terminal_write(buffer);
}

/* Write a 64-bit value as "high:low" (kernel_printf/k_utoa_hex are 32-bit). */
static void write_hex64(uint64_t value)
{
    write_hex32((uint32_t)(value >> 32));
    terminal_put_char(':');
    write_hex32((uint32_t)value);
}

static const char *mmap_type_name(uint32_t type)
{
    switch (type)
    {
    case HAL_MEMORY_AVAILABLE:
        return "available";
    case HAL_MEMORY_ACPI_RECLAIMABLE:
        return "ACPI reclaimable";
    case HAL_MEMORY_ACPI_NVS:
        return "ACPI NVS";
    case HAL_MEMORY_UNUSABLE:
        return "bad RAM";
    default:
        return "reserved";
    }
}

static void cmd_mmap(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    hal_memory_region_t region;

    if (hal_boot_memory_region(0, &region) != 0)
    {
        terminal_write_line("No memory map");
        return;
    }

    terminal_write_line("Base              Length            Type");

    for (unsigned int i = 0; hal_boot_memory_region(i, &region) == 0; i++)
    {
        char buffer[32];

        write_hex64(region.base);
        terminal_put_char(' ');
        write_hex64(region.length);
        terminal_put_char(' ');

        k_utoa(region.type, buffer);
        terminal_write(buffer);
        terminal_write(" (");
        terminal_write(mmap_type_name(region.type));
        terminal_write_line(")");
    }
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

static void cmd_m68k(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    m68k_demo_run();
}

static void cmd_modules(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    hal_boot_module_t module;

    if (hal_boot_module_count() == 0)
    {
        terminal_write_line("No boot modules");
        return;
    }

    for (unsigned int i = 0; hal_boot_module(i, &module) == 0; i++)
    {
        char buffer[32];

        k_utoa(i, buffer);
        terminal_write(buffer);
        terminal_write(": ");
        terminal_write(module.name);
        terminal_write(" at ");
        k_utoa_hex((uint32_t)module.base, buffer);
        terminal_write(buffer);
        terminal_write(", ");
        k_utoa((uint32_t)module.length, buffer);
        terminal_write(buffer);
        terminal_write_line(" bytes");
    }
}

/* Append value as exactly two decimal digits (0-99). */
static char *put_two_digits(char *out, unsigned int value)
{
    *out++ = (char)('0' + value / 10 % 10);
    *out++ = (char)('0' + value % 10);

    return out;
}

static void cmd_date(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    hal_datetime_t now;

    if (hal_clock_now(&now) != 0)
    {
        terminal_write_line("Clock not available");
        return;
    }

    /* YYYY-MM-DD HH:MM:SS UTC */
    char text[24];
    char *out = text;

    out = put_two_digits(out, now.year / 100);
    out = put_two_digits(out, now.year % 100);
    *out++ = '-';
    out = put_two_digits(out, now.month);
    *out++ = '-';
    out = put_two_digits(out, now.day);
    *out++ = ' ';
    out = put_two_digits(out, now.hour);
    *out++ = ':';
    out = put_two_digits(out, now.minute);
    *out++ = ':';
    out = put_two_digits(out, now.second);
    k_strcpy(out, " UTC");

    terminal_write_line(text);
}

static void cmd_disks(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    if (block_device_count() == 0)
    {
        terminal_write_line("No block devices");
        return;
    }

    for (unsigned int i = 0; i < block_device_count(); i++)
    {
        const block_device_t *disk = block_device_get(i);
        char buffer[32];

        terminal_write(disk->name);
        terminal_write(": ");
        k_utoa((uint32_t)disk->block_count, buffer);
        terminal_write(buffer);
        terminal_write(" x ");
        k_utoa(disk->block_size, buffer);
        terminal_write(buffer);
        terminal_write_line(" bytes");
    }
}

/*
 * The shell splits its input at spaces, but HFS names can contain them:
 * join the arguments back together with single spaces.
 */
static void join_args(
    int argc,
    char **argv,
    char *path,
    size_t size)
{
    size_t length = 0;

    path[0] = '\0';

    for (int i = 1; i < argc; i++)
    {
        size_t part = k_strlen(argv[i]);

        if (length + part + 2 > size)
            break;

        if (i > 1)
            path[length++] = ' ';

        k_strcpy(path + length, argv[i]);
        length += part;
    }
}

/* Mac Roman names: show only what the text display can (printable ASCII). */
static void write_mac_name(const char *name)
{
    char text[HFS_NAME_MAX + 1];
    size_t i;

    for (i = 0; name[i] != '\0' && i < HFS_NAME_MAX; i++)
    {
        unsigned char c = (unsigned char)name[i];

        text[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }

    text[i] = '\0';
    terminal_write(text);
}

/* A four-character code such as 'TEXT'. */
static void write_type_code(uint32_t code)
{
    char text[5];

    for (int i = 0; i < 4; i++)
    {
        unsigned char c = (unsigned char)(code >> (24 - 8 * i));

        text[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }

    text[4] = '\0';
    terminal_write(text);
}

static int ls_entry(
    const hfs_entry_t *entry,
    void *context)
{
    (void)context;

    char buffer[16];

    if (entry->is_folder)
    {
        terminal_write("d ");
        write_mac_name(entry->name);
        terminal_write("  ");
        k_utoa(entry->valence, buffer);
        terminal_write(buffer);
        terminal_write_line(" items");

        return 0;
    }

    terminal_write("f ");
    write_mac_name(entry->name);
    terminal_write("  ");
    write_type_code(entry->file_type);
    terminal_write(" ");
    k_utoa(entry->data_length, buffer);
    terminal_write(buffer);

    if (entry->rsrc_length != 0)
    {
        terminal_write("+");
        k_utoa(entry->rsrc_length, buffer);
        terminal_write(buffer);
        terminal_write("r");
    }

    terminal_newline();

    return 0;
}

static hfs_volume_t *require_volume(void)
{
    hfs_volume_t *volume = volume_get();

    if (volume == NULL)
        terminal_write_line("No HFS volume (boot with a disk image module)");

    return volume;
}

static void cmd_ls(
    int argc,
    char **argv)
{
    hfs_volume_t *volume = require_volume();
    char path[64];
    hfs_entry_t entry;

    if (volume == NULL)
        return;

    join_args(argc, argv, path, sizeof(path));

    if (hfs_lookup(volume, path, &entry) != 0)
    {
        terminal_write("Not found: ");
        terminal_write_line(path);
        return;
    }

    if (!entry.is_folder)
    {
        ls_entry(&entry, NULL);
        return;
    }

    if (hfs_list(volume, entry.id, ls_entry, NULL) != 0)
        terminal_write_line("Read error");
}

/* cat stops here so a big file does not flood the screen. */
#define CAT_MAX_BYTES 8192

static void cmd_cat(
    int argc,
    char **argv)
{
    hfs_volume_t *volume = require_volume();
    char path[64];
    hfs_entry_t entry;

    if (volume == NULL)
        return;

    join_args(argc, argv, path, sizeof(path));

    if (path[0] == '\0')
    {
        terminal_write_line("usage: cat <path>");
        return;
    }

    if (hfs_lookup(volume, path, &entry) != 0)
    {
        terminal_write("Not found: ");
        terminal_write_line(path);
        return;
    }

    if (entry.is_folder)
    {
        terminal_write_line("Is a folder");
        return;
    }

    uint32_t offset = 0;

    while (offset < entry.data_length && offset < CAT_MAX_BYTES)
    {
        char chunk[65];
        int got = hfs_read(volume, &entry, HFS_FORK_DATA, offset, chunk,
                           sizeof(chunk) - 1);

        if (got <= 0)
        {
            terminal_write_line("Read error");
            return;
        }

        /* Mac text files end lines with CR; the other bytes that are not
         * printable ASCII (Mac Roman above 0x7F, controls) show as '.'. */
        for (int i = 0; i < got; i++)
        {
            unsigned char c = (unsigned char)chunk[i];

            chunk[i] = c == '\r' ? '\n'
                       : (c == '\n' || (c >= 0x20 && c < 0x7F)) ? (char)c
                                                                : '.';
        }

        chunk[got] = '\0';
        terminal_write(chunk);
        offset += (uint32_t)got;
    }

    terminal_newline();

    if (entry.data_length > CAT_MAX_BYTES)
        terminal_write_line("... (truncated)");
}


static void cmd_mac(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    mac_machine_run(0);
}

static void cmd_cpuinfo(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    static const struct
    {
        uint32_t mask;
        const char *name;
    } flags[] = {
        {HAL_CPU_FEATURE_FPU, "fpu"},
        {HAL_CPU_FEATURE_TSC, "tsc"},
        {HAL_CPU_FEATURE_MSR, "msr"},
        {HAL_CPU_FEATURE_PAE, "pae"},
        {HAL_CPU_FEATURE_APIC, "apic"},
        {HAL_CPU_FEATURE_CMOV, "cmov"},
        {HAL_CPU_FEATURE_PGE, "pge"},
        {HAL_CPU_FEATURE_FXSR, "fxsr"},
        {HAL_CPU_FEATURE_SSE, "sse"},
        {HAL_CPU_FEATURE_SSE2, "sse2"},
        {HAL_CPU_FEATURE_SSE3, "sse3"},
        {HAL_CPU_FEATURE_SSSE3, "ssse3"},
        {HAL_CPU_FEATURE_SSE41, "sse4.1"},
        {HAL_CPU_FEATURE_SSE42, "sse4.2"},
        {HAL_CPU_FEATURE_X2APIC, "x2apic"},
        {HAL_CPU_FEATURE_POPCNT, "popcnt"},
        {HAL_CPU_FEATURE_XSAVE, "xsave"},
        {HAL_CPU_FEATURE_AVX, "avx"},
        {HAL_CPU_FEATURE_NX, "nx"},
        {HAL_CPU_FEATURE_INVARIANT_TSC, "invariant_tsc"},
        {HAL_CPU_FEATURE_HYPERVISOR, "hypervisor"},
    };

    const hal_cpu_info_t *info = hal_cpu_info();
    char buffer[16];

    if (info->vendor[0] == '\0')
    {
        terminal_write_line("CPUID not available");
        return;
    }

    terminal_write("Vendor:   ");
    terminal_write_line(info->vendor);
    terminal_write("Model:    ");
    terminal_write_line(info->brand[0] != '\0' ? info->brand : "(no name)");

    terminal_write("Family ");
    k_utoa(info->family, buffer);
    terminal_write(buffer);
    terminal_write(", model ");
    k_utoa(info->model, buffer);
    terminal_write(buffer);
    terminal_write(", stepping ");
    k_utoa(info->stepping, buffer);
    terminal_write_line(buffer);

    terminal_write("Features:");

    for (unsigned int i = 0; i < sizeof(flags) / sizeof(flags[0]); i++)
    {
        if (info->features & flags[i].mask)
        {
            terminal_write(" ");
            terminal_write(flags[i].name);
        }
    }

    terminal_newline();
}
