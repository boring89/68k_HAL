/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "command.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kernel/terminal/terminal.h"

#include "hal/timer.h"

#include "kernel/lib/k_string.h"
#include "kernel/lib/k_number.h"
#include "kernel/lib/k_memory.h"

#include "kernel/m68k/demo.h"
#include "kernel/mac/machine.h"
#include "kernel/memory/memory.h"
#include "kernel/block/block.h"
#include "kernel/block/cache.h"
#include "kernel/config/config.h"
#include "kernel/fs/macdisk.h"
#include "kernel/mac/pram.h"
#include "kernel/fs/volume.h"
#include "kernel/user/process.h"
#include "kernel/vfs/ehfsfs.h"
#include "kernel/vfs/vfs.h"
#include "kernel/lib/k_errno.h"
#include "hal/boot.h"
#include "hal/clock.h"
#include "hal/acpi.h"
#include "hal/irq.h"
#include "hal/power.h"
#include "hal/cpu.h"
#include "hal/pci.h"
#include "hal/framebuffer.h"
#include "kernel/display/fbcon.h"
#include "kernel/display/fb_pattern.h"
#include "kernel/net/net.h"
#include "kernel/sound/sound.h"
#include "kernel/usb/usb.h"
#include "kernel/console/history.h"
#include "kernel/input/event_buffer.h"
#include "kernel/pci/pci_names.h"

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
static void cmd_acpi(
    int argc,
    char **argv);
static void cmd_run(
    int argc,
    char **argv);
static void cmd_lspci(
    int argc,
    char **argv);
static void cmd_net(
    int argc,
    char **argv);
static void cmd_dhcp(
    int argc,
    char **argv);
static void cmd_ping(
    int argc,
    char **argv);
static void cmd_dns(
    int argc,
    char **argv);
static void cmd_beep(
    int argc,
    char **argv);
static void cmd_video(
    int argc,
    char **argv);
static void cmd_http(
    int argc,
    char **argv);
static void cmd_usb(
    int argc,
    char **argv);
static void cmd_mac(
    int argc,
    char **argv);
static void cmd_cd(
    int argc,
    char **argv);
static void cmd_pwd(
    int argc,
    char **argv);

static void cmd_dmesg(
    int argc,
    char **argv);
static void cmd_diag(
    int argc,
    char **argv);
static void cmd_poweroff(
    int argc,
    char **argv);
static void cmd_reboot(
    int argc,
    char **argv);
static void cmd_irqs(
    int argc,
    char **argv);

static void cmd_macsys(
    int argc,
    char **argv);
static void cmd_mount(
    int argc,
    char **argv);
static void cmd_config(
    int argc,
    char **argv);
static void cmd_cache(
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
         "Run the Macintosh IIci on boot modules mac_rom and mac_disk (` stops): mac [scale=1|1.5|2|auto] [res=WxH] [depth=32] [cards=2] [builtin] [ram=MB]",
         cmd_mac},
        {COMMAND_CPUINFO,
         "cpuinfo",
         "Display the CPU model and features",
         cmd_cpuinfo},
        {COMMAND_ACPI,
         "acpi",
         "Display processors and interrupt controllers from the ACPI tables",
         cmd_acpi},
        {COMMAND_RUN,
         "run",
         "Run a program of the file tree (run <path> [arguments...])",
         cmd_run},
        {COMMAND_LSPCI,
         "lspci",
         "List PCI devices (lspci -v also shows the base address registers)",
         cmd_lspci},
        {COMMAND_NET,
         "net",
         "Show the network card and its address",
         cmd_net},
        {COMMAND_DHCP,
         "dhcp",
         "Get an address from a DHCP server",
         cmd_dhcp},
        {COMMAND_PING,
         "ping",
         "Send an ICMP echo request (ping <a.b.c.d>)",
         cmd_ping},
        {COMMAND_DNS,
         "dns",
         "Look up the address of a name (dns <name>)",
         cmd_dns},
        {COMMAND_BEEP,
         "beep",
         "Play a tone (beep [Hz [milliseconds]])",
         cmd_beep},
        {COMMAND_VIDEO,
         "video",
         "Show the screen mode, or set one and draw a test pattern (video <width> <height>, video list, video edid, video native)",
         cmd_video},
        {COMMAND_HTTP,
         "http",
         "Fetch a web page over TCP (http <host> [port [path]])",
         cmd_http},
        {COMMAND_USB,
         "usb",
         "List the USB host controllers",
         cmd_usb},
        {COMMAND_CD,
         "cd",
         "Change the current directory of the file tree (cd [path]; no path = /)",
         cmd_cd},
        {COMMAND_PWD,
         "pwd",
         "Show the current directory of the file tree",
         cmd_pwd},
        {COMMAND_DMESG,
         "dmesg",
         "Show the boot messages again (dmesg [number of last lines])",
         cmd_dmesg},
        {COMMAND_DIAG,
         "diag",
         "Hardware diagnostics, one page per subsystem (any key = next page)",
         cmd_diag},
        {COMMAND_POWEROFF,
         "poweroff",
         "Turn the computer off (ACPI soft off)",
         cmd_poweroff},
        {COMMAND_REBOOT,
         "reboot",
         "Restart the computer",
         cmd_reboot},
        {COMMAND_IRQS,
         "irqs",
         "Interrupts counted per line since boot (lines 0-15 ISA, 16 and up message signalled)",
         cmd_irqs},
        {COMMAND_MACSYS,
         "macsys",
         "The Mac system disk: macsys status | reset (drop the Mac's changes) | promote (keep them in the other System slot) | switch a|b [force] | pram [reset] (the Mac's parameter RAM)",
         cmd_macsys},
        {COMMAND_MOUNT,
         "mount",
         "Use the HFS volume of a block device for ls and cat (mount <device>, see disks)",
         cmd_mount},
        {COMMAND_CONFIG,
         "config",
         "Show the settings (68k_hal.cfg and the kernel command line)",
         cmd_config},
        {COMMAND_CACHE,
         "cache",
         "Disk cache statistics; cache drop forgets every cached block; cache bench <device> [MB] times two reads",
         cmd_cache}};

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

static const char *const *shell_environment(void)
{
    static const char *const environment[] = {
        "PATH=/bin", "HOME=/tmp", "TERM=vt100", NULL};

    return environment;
}

/* After a program ran: say why it did not, or how it ended if not with 0. */
static void report_status(const char *name, int32_t status)
{
    char number[16];

    if (status < 0)
    {
        terminal_write(name);
        terminal_write(": cannot run (error ");
        k_utoa((unsigned int)-status, number);
        terminal_write(number);
        terminal_write_line(")");
    }
    else if (status != 0)
    {
        terminal_write("[exit ");
        k_utoa((unsigned int)status, number);
        terminal_write(number);
        terminal_write_line("]");
    }
}

/* The program a command name stands for: a path as it is, any other name in /bin. */
static bool find_program(const char *name, char *path)
{
    vfs_stat_t stat;

    if (name[0] == '/')
        k_strcpy(path, name);
    else if (k_strlen(name) + 5 < VFS_PATH_MAX)
    {
        k_strcpy(path, "/bin/");
        k_strcpy(path + 5, name);
    }
    else
        return false;

    return vfs_stat(path, &stat) == 0 && (stat.mode & VFS_S_IFMT) == VFS_S_IFREG;
}

#define PIPELINE_MAX 4

typedef struct
{
    const char *argv[SHELL_MAX_ARGS + 1];
    int argc;
    const char *input;  /* < file */
    const char *output; /* > file or >> file */
    bool append;
} pipeline_stage_t;

/*
 * A line with "|", "<", ">" or ">>" (each as a word of its own). The stages run at the same time,
 * the output of each one a pipe to the next one's input (user_run_job). Only programs take part,
 * not built-in commands.
 */
static void run_pipeline(shell_command_t *cmd)
{
    pipeline_stage_t stages[PIPELINE_MAX];
    int count = 1;

    k_memset(stages, 0, sizeof(stages));

    for (int i = 0; i < cmd->argc; i++)
    {
        pipeline_stage_t *stage = &stages[count - 1];
        const char *word = cmd->argv[i];

        if (k_strcmp(word, "|") == 0)
        {
            if (stage->argc == 0 || count == PIPELINE_MAX)
            {
                terminal_write_line("shell: bad pipeline");

                return;
            }

            count++;
        }
        else if (k_strcmp(word, "<") == 0 || k_strcmp(word, ">") == 0 || k_strcmp(word, ">>") == 0)
        {
            if (i + 1 >= cmd->argc)
            {
                terminal_write_line("shell: missing file name");

                return;
            }

            if (word[0] == '<')
                stage->input = cmd->argv[++i];
            else
            {
                stage->append = word[1] == '>';
                stage->output = cmd->argv[++i];
            }
        }
        else
        {
            stage->argv[stage->argc++] = word;
        }
    }

    for (int i = 0; i < count; i++)
    {
        if (stages[i].argc == 0)
        {
            terminal_write_line("shell: missing command");

            return;
        }
    }

    char paths[PIPELINE_MAX][VFS_PATH_MAX];
    user_job_stage_t job[PIPELINE_MAX];

    for (int i = 0; i < count; i++)
    {
        if (!find_program(stages[i].argv[0], paths[i]))
        {
            terminal_write(stages[i].argv[0]);
            terminal_write_line(": not a program");

            return;
        }

        stages[i].argv[stages[i].argc] = NULL;
        job[i].path = paths[i];
        job[i].argv = stages[i].argv;
        job[i].stdin_path = stages[i].input;
        job[i].stdout_path = stages[i].output;
        job[i].append = stages[i].output != NULL && stages[i].append;
    }

    int32_t status[PIPELINE_MAX];

    if (user_run_job(job, (unsigned int)count, shell_environment(), status) != 0)
    {
        terminal_write_line("shell: cannot run the pipeline");

        return;
    }

    for (int i = 0; i < count; i++)
    {
        report_status(stages[i].argv[0], status[i]);

        /* A stage that could not start ended the line: the later ones did not run. */
        if (status[i] < 0)
            break;
    }
}

void shell_dispatch(shell_command_t *cmd)
{
    if (cmd->argc == 0)
        return;

    for (int i = 0; i < cmd->argc; i++)
    {
        if (k_strcmp(cmd->argv[i], "|") == 0 || k_strcmp(cmd->argv[i], "<") == 0 ||
            k_strcmp(cmd->argv[i], ">") == 0 || k_strcmp(cmd->argv[i], ">>") == 0)
        {
            run_pipeline(cmd);

            return;
        }
    }

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

    /*
     * Not built in: a program of the file tree. A name with a slash is a path; any other is looked
     * for in /bin (where the toolbox's commands are, as links to /bin/box: userland/box.c).
     */
    char path[VFS_PATH_MAX];

    if (find_program(cmd->argv[0], path))
    {
        const char *args[SHELL_MAX_ARGS + 1];

        for (int i = 0; i < cmd->argc; i++)
            args[i] = cmd->argv[i];

        args[cmd->argc] = NULL;

        report_status(cmd->argv[0], user_run(path, args, shell_environment()));

        return;
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


/* "WxH" (for example 1280x720) into its two numbers. Returns 0, or -1 if it is not one. */
static int parse_resolution(
    const char *text,
    uint32_t *width,
    uint32_t *height)
{
    uint32_t value[2] = {0, 0};
    int part = 0;

    for (; *text != '\0'; text++)
    {
        if (*text == 'x' && part == 0)
            part = 1;
        else if (*text >= '0' && *text <= '9' && value[part] < 100000)
            value[part] = value[part] * 10 + (uint32_t)(*text - '0');
        else
            return -1;
    }
    if (part != 1 || value[0] == 0 || value[1] == 0)
        return -1;
    *width = value[0];
    *height = value[1];
    return 0;
}

static void cmd_mac(
    int argc,
    char **argv)
{
    uint32_t halves = 0;
    uint32_t width = 0, height = 0;
    bool builtin = false;
    int bits = 0;

    size_t ram_mb = 0;

    mac_machine_set_depth(0);
    mac_machine_set_cards(1);

    /* The memory: ram=MB, else mac_ram of the settings, else the default. */
    if (config_get("mac_ram") == NULL || parse_size(config_get("mac_ram"), &ram_mb) != 0 ||
        mac_machine_set_ram_mb((uint32_t)ram_mb) != 0)
        mac_machine_set_ram_mb(0);

    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];

        if (k_strncmp(arg, "ram=", 4) == 0 && parse_size(arg + 4, &ram_mb) == 0 && ram_mb <= 4096 &&
            mac_machine_set_ram_mb((uint32_t)ram_mb) == 0)
            continue;

        if (k_strncmp(arg, "scale=", 6) == 0)
            arg += 6;

        if (k_strcmp(arg, "auto") == 0 || k_strcmp(arg, "0") == 0)
            halves = 0;
        else if (k_strcmp(arg, "res=auto") == 0)
            mac_machine_forget_mode();
        else if (k_strcmp(arg, "0.5") == 0)
            halves = 1;
        else if (k_strcmp(arg, "builtin") == 0)
            builtin = true;
        else if (k_strcmp(arg, "cards=1") == 0 || k_strcmp(arg, "cards=2") == 0)
            mac_machine_set_cards((uint32_t)(arg[6] - '0'));
        else if (k_strncmp(arg, "depth=", 6) == 0 && (k_atoi(arg + 6, &bits), bits > 0) &&
                 mac_machine_set_depth((uint32_t)bits) == 0)
            continue;
        else if (k_strncmp(arg, "res=", 4) == 0 && parse_resolution(arg + 4, &width, &height) == 0 &&
                 mac_machine_set_resolution(width, height) == 0)
            continue;
        else if (arg[0] >= '1' && arg[0] <= '4' && (arg[1] == '\0' || (arg[1] == '.' && arg[2] == '5' && arg[3] == '\0')))
            halves = (uint32_t)(arg[0] - '0') * 2 + (arg[1] == '.' ? 1 : 0);
        else
        {
            terminal_write_line("usage: mac [scale=0.5|1|1.5|2|2.5|3|4|auto] [res=WxH|auto] [depth=1|2|4|8|16|32] [cards=1|2] [builtin] [ram=MB]");
            terminal_write_line("  ram: the Mac's memory, 8 to 1024 MB (default 128, or mac_ram of 68k_hal.cfg)");
            terminal_write_line("  res: 640x480 800x600 1024x768 1152x870 1280x720 1280x960 1280x1024 1440x900");
            terminal_write_line("       1600x1200 1920x1080 1920x1200 2560x1440 3840x2160 (default: as the last run ended,");
            terminal_write_line("       the first time the one that fills the screen; res=auto forgets the last one)");
            terminal_write_line("  Ctrl+Alt+PgUp/PgDn/Home: larger/smaller/largest while it runs; ` stops");
            return;
        }
    }
    if (width == 0)
        mac_machine_set_resolution(0, 0);

    /* The Mac takes the screen (and gives it back, with the text it had: machine.c). */
    mac_machine_set_scale_halves(halves);
    mac_machine_set_builtin(builtin);
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

static void write_number_line(const char *label, uint32_t value, int hex)
{
    char buffer[16];

    if (hex)
        k_utoa_hex(value, buffer);
    else
        k_utoa(value, buffer);

    terminal_write(label);
    terminal_write_line(buffer);
}

static void cmd_acpi(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    const hal_acpi_info_t *info = hal_acpi_info();

    if (info == NULL)
    {
        terminal_write_line("No ACPI tables");
        return;
    }

    terminal_write("OEM:            ");
    terminal_write_line(info->oem_id);
    write_number_line("ACPI revision:  ", info->revision, 0);
    write_number_line("Processors:     ", info->cpu_count, 0);
    write_number_line("Local APIC at:  0x", info->lapic_base, 1);

    for (uint32_t i = 0; i < info->ioapic_count; i++)
    {
        write_number_line("I/O APIC id:    ", info->ioapic_id[i], 0);
        write_number_line("  at:           0x", info->ioapic_base[i], 1);
        write_number_line("  first input:  ", info->ioapic_gsi_base[i], 0);
    }

    for (uint32_t i = 0; i < info->irq_override_count; i++)
    {
        write_number_line("IRQ override:   ISA ", info->irq_override[i].source, 0);
        write_number_line("  -> input:     ", info->irq_override[i].gsi, 0);
    }

    terminal_write("Interrupts:     ");
    terminal_write_line(hal_irq_controller());

    if (info->s5_valid)
    {
        write_number_line("Soft off:       ACPI S5, PM1a port 0x", info->pm1a_control_port, 1);
        write_number_line("  sleep type:   ", info->s5_type_a, 0);
    }
    else
    {
        terminal_write_line("Soft off:       no \\_S5 found (virtual machine ports only)");
    }

    terminal_write_line(info->reset_valid ? "Reset:          ACPI reset register" : "Reset:          keyboard controller / port 0xCF9");
}

static void cmd_run(
    int argc,
    char **argv)
{
    if (argc < 2)
    {
        terminal_write_line("usage: run <path> [arguments...]");
        return;
    }

    /* The program sees the path as argv[0], then the arguments. */
    const char *args[SHELL_MAX_ARGS + 1];

    for (int i = 1; i < argc; i++)
        args[i - 1] = argv[i];

    args[argc - 1] = NULL;

    report_status(argv[1], user_run(argv[1], args, shell_environment()));
}

static void cmd_cd(
    int argc,
    char **argv)
{
    int32_t status = vfs_chdir(argc > 1 ? argv[1] : "/");

    if (status != 0)
    {
        terminal_write("cd: ");
        terminal_write(argc > 1 ? argv[1] : "/");
        terminal_write_line(status == -K_ENOTDIR ? ": not a directory" : ": no such directory");
    }
}

static void cmd_pwd(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    terminal_write_line(vfs_getcwd());
}

static void write_hex_padded(uint32_t value, int digits)
{
    char buffer[16];

    k_utoa_hex(value, buffer);

    for (int i = (int)k_strlen(buffer); i < digits; i++)
        terminal_put_char('0');

    terminal_write(buffer);
}

static void cmd_lspci(
    int argc,
    char **argv)
{
    int verbose = argc > 1 && k_strcmp(argv[1], "-v") == 0;

    if (hal_pci_device_count() == 0)
    {
        terminal_write_line("No PCI devices");
        return;
    }

    for (unsigned int i = 0; i < hal_pci_device_count(); i++)
    {
        hal_pci_device_t dev;

        hal_pci_device(i, &dev);

        /* bus:device.function  class  [vendor:device] */
        write_hex_padded(dev.bus, 2);
        terminal_put_char(':');
        write_hex_padded(dev.device, 2);
        terminal_put_char('.');
        write_hex_padded(dev.function, 1);
        terminal_write(" ");
        terminal_write(pci_class_name(dev.class_code, dev.subclass));
        terminal_write(": ");
        terminal_write(pci_vendor_name(dev.vendor_id));
        terminal_write(" [");
        write_hex_padded(dev.vendor_id, 4);
        terminal_put_char(':');
        write_hex_padded(dev.device_id, 4);
        terminal_write_line("]");

        if (!verbose)
            continue;

        if (dev.irq_pin != 0)
        {
            terminal_write("        IRQ ");

            char number[16];

            k_utoa(dev.irq_line, number);
            terminal_write_line(number);
        }

        for (int b = 0; b < 6; b++)
        {
            if (dev.bar[b].type == HAL_PCI_BAR_NONE)
                continue;

            char number[16];

            terminal_write(dev.bar[b].type == HAL_PCI_BAR_IO ? "        I/O ports at " : "        Memory at ");
            /* A 64-bit BAR can be above 4 GiB (a workstation with "Above 4G Decoding"): all 16 digits then. */
            if (dev.bar[b].base >> 32)
                write_hex_padded((uint32_t)(dev.bar[b].base >> 32), 8);
            write_hex_padded((uint32_t)dev.bar[b].base, 8);
            terminal_write(" (");
            k_utoa((uint32_t)dev.bar[b].size, number);
            terminal_write(number);
            terminal_write_line(" bytes)");
        }
    }
}

/* Parse "a.b.c.d" into four bytes. Returns 0 or -1. */
static int parse_ip(const char *text, uint8_t ip[4])
{
    for (int i = 0; i < 4; i++)
    {
        unsigned int value = 0;
        int digits = 0;

        while (*text >= '0' && *text <= '9' && digits < 4)
        {
            value = value * 10 + (unsigned int)(*text - '0');
            text++;
            digits++;
        }

        if (digits == 0 || value > 255 || (i < 3 && *text != '.') || (i == 3 && *text != '\0'))
            return -1;

        ip[i] = (uint8_t)value;

        if (i < 3)
            text++;
    }

    return 0;
}

static void write_ip_line(const char *label, const uint8_t ip[4])
{
    char text[16];

    net_format_ip(ip, text);
    terminal_write(label);
    terminal_write_line(text);
}

static void cmd_net(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    net_device_t *device = net_device_get();

    if (device == NULL)
    {
        terminal_write_line("No network card");
        return;
    }

    terminal_write("Card:     ");
    terminal_write_line(device->name);
    terminal_write("MAC:      ");

    for (int i = 0; i < 6; i++)
    {
        write_hex_padded(device->mac[i], 2);

        if (i < 5)
            terminal_put_char(':');
    }

    terminal_newline();

    const net_config_t *config = net_config();

    if (!config->configured)
    {
        terminal_write_line("No address yet (run dhcp)");
        return;
    }

    write_ip_line("Address:  ", config->ip);
    write_ip_line("Mask:     ", config->mask);
    write_ip_line("Gateway:  ", config->gateway);
    write_ip_line("DNS:      ", config->dns);
}

static void cmd_dhcp(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    if (net_device_get() == NULL)
    {
        terminal_write_line("No network card");
        return;
    }

    if (net_dhcp(10000) != 0)
    {
        terminal_write_line("dhcp: no answer");
        return;
    }

    write_ip_line("Address: ", net_config()->ip);
}

static void cmd_ping(
    int argc,
    char **argv)
{
    uint8_t ip[4];

    if (argc < 2 || parse_ip(argv[1], ip) != 0)
    {
        terminal_write_line("usage: ping <a.b.c.d>");
        return;
    }

    for (int i = 0; i < 3; i++)
    {
        uint32_t microseconds;
        char number[16];

        if (net_ping(ip, 2000, &microseconds) == 0)
        {
            terminal_write("reply from ");
            terminal_write(argv[1]);
            terminal_write(": time=");
            k_utoa(microseconds, number);
            terminal_write(number);
            terminal_write_line(" us");
        }
        else
        {
            terminal_write_line("no reply");
        }
    }
}

static void cmd_dns(
    int argc,
    char **argv)
{
    uint8_t ip[4];

    if (argc < 2)
    {
        terminal_write_line("usage: dns <name>");
        return;
    }

    int status = net_dns_resolve(argv[1], ip, 5000);

    if (status == 0)
        write_ip_line("address: ", ip);
    else if (status == 1)
        terminal_write_line("no such name");
    else
        terminal_write_line("dns: no answer (is the network up? run dhcp)");
}

static void cmd_beep(
    int argc,
    char **argv)
{
    int frequency = 440;
    int milliseconds = 300;

    if (argc > 1)
        k_atoi(argv[1], &frequency);

    if (argc > 2)
        k_atoi(argv[2], &milliseconds);

    if (frequency < 20 || frequency > 20000 || milliseconds < 1 || milliseconds > 10000)
    {
        terminal_write_line("usage: beep [20..20000 Hz [1..10000 ms]]");
        return;
    }

    if (sound_beep((uint32_t)frequency, (uint32_t)milliseconds, 50) != 0)
        terminal_write_line("No sound card");
}

/* "WxH" written to the terminal. */
static void write_size(
    uint32_t width,
    uint32_t height)
{
    char number[16];

    k_utoa(width, number);
    terminal_write(number);
    terminal_write("x");
    k_utoa(height, number);
    terminal_write(number);
}

/* Switch the screen to width x height and draw the test pattern. Returns 0 or -1 (with a message). */
static int video_switch(
    uint32_t width,
    uint32_t height)
{
    hal_framebuffer_t fb;

    if (width < 1 || height < 1 || hal_framebuffer_set_mode(width, height) != 0)
    {
        terminal_write_line("video: that mode is not available");
        return -1;
    }
    if (hal_framebuffer_get(&fb) == 0)
        fb_draw_test_pattern(&fb);
    return 0;
}

/*
 * video                 the current mode
 * video <w> <h>         switch (the Bochs/QEMU VGA, or a VBE mode of the video BIOS)
 * video list            the VBE modes (32 bits per pixel) of the video BIOS
 * video edid            the monitor's native resolution (EDID through VBE/DDC)
 * video native          switch to the native resolution
 * VBE needs a legacy BIOS (or CSM) boot: under UEFI there is no video BIOS, and only the firmware's mode.
 */
static void cmd_video(
    int argc,
    char **argv)
{
    hal_framebuffer_t fb;
    uint32_t width, height;

    if (argc == 2 && k_strcmp(argv[1], "list") == 0)
    {
        static hal_video_mode_t modes[64];
        int count = hal_framebuffer_modes(modes, 64);

        if (count < 0)
        {
            terminal_write_line("video: no video BIOS (VBE needs a legacy BIOS or CSM boot; UEFI keeps its mode)");
            return;
        }
        for (int i = 0; i < count; i++)
        {
            terminal_write("  ");
            write_size(modes[i].width, modes[i].height);
            terminal_newline();
        }
        terminal_write("video: ");
        terminal_write_line(count == 0 ? "no 32-bit VBE modes" : "VBE modes with 32 bits per pixel");
        return;
    }
    if (argc == 2 && (k_strcmp(argv[1], "edid") == 0 || k_strcmp(argv[1], "native") == 0))
    {
        if (hal_framebuffer_native(&width, &height) != 0)
        {
            terminal_write_line("video: cannot read the monitor's EDID (needs VBE/DDC: a legacy BIOS boot)");
            return;
        }
        terminal_write("video: native resolution ");
        write_size(width, height);
        terminal_newline();
        if (k_strcmp(argv[1], "native") == 0)
            video_switch(width, height);
        return;
    }
    if (argc >= 3)
    {
        int w = 0;
        int h = 0;

        k_atoi(argv[1], &w);
        k_atoi(argv[2], &h);
        if (w < 1 || h < 1 || video_switch((uint32_t)w, (uint32_t)h) != 0)
            return;
    }

    if (hal_framebuffer_get(&fb) != 0)
    {
        terminal_write_line("video: text mode (no framebuffer)");
        return;
    }

    terminal_write("video: ");
    write_size(fb.width, fb.height);
    terminal_write_line(" 32 bpp");
}

/* GET a page: host is an address or a name; the answer (headers and body) is written to the terminal. */
static void cmd_http(
    int argc,
    char **argv)
{
    uint8_t ip[4];

    if (argc < 2)
    {
        terminal_write_line("usage: http <host> [port [path]]");
        return;
    }

    if (parse_ip(argv[1], ip) != 0)
    {
        int status = net_dns_resolve(argv[1], ip, 5000);

        if (status != 0)
        {
            terminal_write_line("http: cannot find that name");
            return;
        }
    }

    int port = 80;
    const char *path = "/";

    if (argc > 2)
        k_atoi(argv[2], &port);

    if (argc > 3)
        path = argv[3];

    char request[256];
    size_t length = 0;
    const char *parts[] = {"GET ", path, " HTTP/1.0\r\nHost: ", argv[1], "\r\nConnection: close\r\n\r\n"};

    for (unsigned int i = 0; i < sizeof(parts) / sizeof(parts[0]); i++)
    {
        size_t n = k_strlen(parts[i]);

        if (length + n >= sizeof(request))
        {
            terminal_write_line("http: the request is too long");
            return;
        }

        k_memcpy(request + length, parts[i], n);
        length += n;
    }

    int connection = net_tcp_connect(ip, (uint16_t)port, 5000);

    if (connection < 0)
    {
        terminal_write_line("http: cannot connect");
        return;
    }

    if (net_tcp_send_all(connection, request, length, 5000) != 0)
    {
        terminal_write_line("http: cannot send");
        net_tcp_close(connection);
        return;
    }

    char buffer[256];
    size_t shown = 0;
    int n;

    while ((n = net_tcp_recv(connection, buffer, sizeof(buffer) - 1, 5000)) > 0)
    {
        for (int i = 0; i < n && shown < 4000; i++, shown++)
        {
            char c = buffer[i];

            if (c != '\r')
                terminal_put_char(c);
        }
    }

    terminal_newline();

    if (n != TCP_EOF)
        terminal_write_line("http: the connection broke off");

    net_tcp_close(connection);
}

static void cmd_usb(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    if (usb_host_count() == 0)
    {
        terminal_write_line("No USB host controllers");
        return;
    }

    for (unsigned int i = 0; i < usb_host_count(); i++)
    {
        usb_host_info_t host;
        char number[16];

        usb_host_get(i, &host);

        /* 00:03.0 xHCI (USB 3.x) version 1.0, 4 ports, 64 slots */
        write_hex_padded(host.bus, 2);
        terminal_put_char(':');
        write_hex_padded(host.device, 2);
        terminal_put_char('.');
        write_hex_padded(host.function, 1);
        terminal_write(" ");
        terminal_write(usb_host_type_name(host.type));
        terminal_write(" (USB ");
        terminal_write(usb_host_speed_name(host.type));
        terminal_write(") version ");
        k_utoa(host.version >> 8, number);
        terminal_write(number);
        terminal_put_char('.');
        k_utoa((host.version >> 4) & 0xF, number);
        terminal_write(number);
        terminal_write(", ");
        k_utoa(host.ports, number);
        terminal_write(number);
        terminal_write(" ports");

        if (host.slots != 0)
        {
            k_utoa(host.slots, number);
            terminal_write(", ");
            terminal_write(number);
            terminal_write(" device slots");
        }

        terminal_newline();
    }

    /* Below the controllers: what is plugged in. */
    for (unsigned int i = 0; i < usb_device_count(); i++)
    {
        usb_device_info_t device;
        char number[16];

        usb_device_get(i, &device);
        terminal_write("  port ");
        k_utoa(device.port, number);
        terminal_write(number);
        terminal_write(": ");
        write_hex_padded(device.vendor_id, 4);
        terminal_put_char(':');
        write_hex_padded(device.product_id, 4);
        terminal_write(", ");
        terminal_write(device.speed == 1 ? "full" : device.speed == 2 ? "low" : device.speed == 3 ? "high" : "super");
        terminal_write(" speed, ");
        terminal_write_line(device.use);
    }
}

static void cmd_dmesg(
    int argc,
    char **argv)
{
    static char buffer[CONSOLE_HISTORY_SIZE + 1];
    int lines = 0;

    if (argc > 1)
        k_atoi(argv[1], &lines);

    console_history_last_lines(buffer, sizeof(buffer), lines > 0 ? (unsigned int)lines : 0);
    terminal_write(buffer);
}

/* Wait for a key press or for `seconds` to pass, whichever is first; keep the network and USB going meanwhile. */
static void wait_for_key(unsigned int seconds)
{
    uint64_t end = hal_timer_ns() + (uint64_t)seconds * 1000000000u;

    while (hal_timer_ns() < end)
    {
        input_event_t event;
        int pressed = 0;

        while (input_event_pop(&event) == 0)
        {
            if (event.type == INPUT_EVENT_KEYBOARD && event.data.keyboard.pressed)
                pressed = 1;
        }

        if (pressed)
            return;

        net_poll();
        usb_poll();
        hal_cpu_enable_interrupts();
        hal_cpu_idle();
    }
}

#define DIAG_PAGE_SECONDS 10
#define DIAG_BOOT_LOG_LINES 22

static void diag_page(unsigned int number, unsigned int total, const char *title)
{
    char text[16];

    terminal_clear();
    terminal_write("=== 68k_HAL diagnostics ");
    k_utoa(number, text);
    terminal_write(text);
    terminal_write("/");
    k_utoa(total, text);
    terminal_write(text);
    terminal_write(": ");
    terminal_write(title);
    terminal_write_line(" (any key = next page) ===");
}

static void run_named(const char *name, int argc, char **argv)
{
    for (int i = 0; i < COMMAND_TABLE_SIZE; i++)
    {
        if (k_strcmp(commands[i].name, name) == 0)
        {
            commands[i].handler(argc, argv);

            return;
        }
    }
}

void shell_run_diagnostics(void)
{
    static const char *const pages[] = {"cpuinfo", "acpi", "lspci -v", "usb", "irqs", "disks", "net"};
    unsigned int total = sizeof(pages) / sizeof(pages[0]) + 2;
    char *none[1] = {0};

    for (unsigned int i = 0; i < sizeof(pages) / sizeof(pages[0]); i++)
    {
        diag_page(i + 1, total, pages[i]);

        if (k_strcmp(pages[i], "lspci -v") == 0)
        {
            char *args[3] = {"lspci", "-v", 0};

            run_named("lspci", 2, args);
        }
        else
        {
            run_named(pages[i], 1, none);
        }

        wait_for_key(DIAG_PAGE_SECONDS);
    }

    /* The boot messages: what each driver reported while starting. The end of the log first (the drivers), then the start. */
    static char log[CONSOLE_HISTORY_SIZE + 1];

    console_history_last_lines(log, sizeof(log), 0);

    size_t length = k_strlen(log);
    unsigned int line_count = 0;

    for (size_t i = 0; i < length; i++)
        line_count += log[i] == '\n';

    for (unsigned int part = 0; part < 2; part++)
    {
        diag_page(sizeof(pages) / sizeof(pages[0]) + 1 + part, total, part == 0 ? "boot messages (start)" : "boot messages (end)");

        unsigned int first = part == 0 ? 0 : (line_count > DIAG_BOOT_LOG_LINES ? line_count - DIAG_BOOT_LOG_LINES : 0);
        unsigned int last = part == 0 ? (line_count > DIAG_BOOT_LOG_LINES ? DIAG_BOOT_LOG_LINES : line_count) : line_count;
        unsigned int line = 0;

        for (size_t i = 0; i < length && line < last; i++)
        {
            if (line >= first)
                terminal_put_char(log[i]);

            line += log[i] == '\n';
        }

        wait_for_key(DIAG_PAGE_SECONDS);
    }

    terminal_clear();
}

static void cmd_diag(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    shell_run_diagnostics();
}

static void cmd_poweroff(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    terminal_write_line("Powering off...");
    ehfsfs_unmount();
    hal_power_off();
}

static void cmd_reboot(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    terminal_write_line("Restarting...");
    ehfsfs_unmount();
    hal_power_reboot();
}

static void cmd_irqs(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    terminal_write("Controller: ");
    terminal_write_line(hal_irq_controller());

    for (unsigned int line = 0; line < HAL_IRQ_LINE_COUNT; line++)
    {
        unsigned int count = hal_irq_line_count(line);

        if (count != 0)
        {
            write_number_line("line ", line, 0);
            write_number_line("  count: ", count, 0);
        }
    }

    write_number_line("total: ", hal_irq_count(), 0);
}

static void macsys_progress(unsigned int percent)
{
    write_number_line("macsys: promote ", percent, 0);
}

static void macsys_report(int result, const char *done)
{
    if (result == 0)
        terminal_write_line(done);
    else if (result == MACDISK_NOT_SAVED)
        terminal_write_line("macsys: done, but grubenv could not be written: the next boot uses the old System slot");
    else if (result == MACDISK_OVERLAY_NOT_EMPTY)
        terminal_write_line("macsys: the overlay holds the Mac's changes: 'macsys promote' keeps them, 'macsys switch <slot> force' drops them");
    else
        terminal_write_line("macsys: failed");
}

static void cmd_macsys(
    int argc,
    char **argv)
{
    if (!macdisk_present())
    {
        terminal_write_line("macsys: no 68k_HAL disk (GPT with System and Overlay partitions)");
        return;
    }

    if (argc >= 2 && k_strcmp(argv[1], "reset") == 0)
    {
        if (macdisk_reset() == 0)
            terminal_write_line("macsys: the system overlay is empty; the Mac starts from the original System image");
        else
            terminal_write_line("macsys: reset failed");
        return;
    }

    if (argc >= 2 && k_strcmp(argv[1], "pram") == 0)
    {
        uint8_t xpram[MAC_PRAM_SIZE];

        if (argc >= 3 && k_strcmp(argv[2], "reset") == 0)
        {
            if (mac_pram_clear(macdisk_boot()) == 0)
                terminal_write_line("macsys: the parameter RAM is empty; the Mac starts with fresh settings");
            else
                terminal_write_line("macsys: cannot write 68k_hal/pram on the Boot partition");
            return;
        }
        terminal_write("Parameter RAM: ");
        terminal_write_line(mac_pram_status_name(mac_pram_load(macdisk_boot(), xpram)));
        if (k_strcmp(config_get_or("mac_pram", "on"), "off") == 0)
            terminal_write_line("  not used: mac_pram = off");
        return;
    }

    if (argc >= 3 && k_strcmp(argv[1], "switch") == 0 &&
        (k_strcmp(argv[2], "a") == 0 || k_strcmp(argv[2], "b") == 0))
    {
        uint32_t slot = argv[2][0] == 'b' ? MACDISK_SLOT_B : MACDISK_SLOT_A;
        int force = argc >= 4 && k_strcmp(argv[3], "force") == 0;

        macsys_report(macdisk_switch(slot, force), "macsys: switched; the overlay is empty and belongs to the new System slot");
        return;
    }

    if (argc >= 2 && k_strcmp(argv[1], "promote") == 0)
    {
        terminal_write_line("macsys: copying the system disk (with the Mac's changes) into the other System slot...");
        macsys_report(macdisk_promote(macsys_progress), "macsys: promoted; the other slot is active, the overlay is empty, the old slot is kept");
        return;
    }

    if (argc >= 2 && k_strcmp(argv[1], "status") != 0)
    {
        terminal_write_line("usage: macsys status | reset | promote | switch a|b [force] | pram [reset]");
        return;
    }

    grubenv_t env;
    int have_env = macdisk_env_read(&env) == 0;

    terminal_write("System slot: ");
    terminal_write_line(macdisk_slot() == MACDISK_SLOT_A ? "A" : "B");

    for (uint32_t slot = MACDISK_SLOT_A; slot <= MACDISK_SLOT_B; slot++)
    {
        block_device_t *dev = macdisk_slot_device(slot);

        terminal_write(slot == MACDISK_SLOT_A ? "  System A: " : "  System B: ");
        if (dev == NULL)
            terminal_write_line("missing");
        else
            write_number_line(slot == macdisk_slot() ? "active, blocks " : "blocks ", (uint32_t)dev->block_count, 0);
    }

    terminal_write("Overlay: ");
    terminal_write_line(overlay_status_name(macdisk_overlay_status()));

    const overlay_t *ov = macdisk_overlay();

    if (ov != NULL)
    {
        write_number_line("  blocks written: ", overlay_used_blocks(ov), 0);
        write_number_line("  blocks free:    ", overlay_capacity_blocks(ov) - overlay_used_blocks(ov), 0);
    }
    else if (macdisk_overlay_status() == OVERLAY_MISMATCH)
    {
        terminal_write_line("  The overlay belongs to another System image and is not applied.");
        terminal_write_line("  'macsys reset' empties it and binds it to this image.");
    }

    uint8_t xpram[MAC_PRAM_SIZE];

    terminal_write("Parameter RAM: ");
    terminal_write_line(mac_pram_status_name(mac_pram_load(macdisk_boot(), xpram)));

    terminal_write("Kernel slot: ");
    terminal_write(config_get_or("kernel_slot", "?"));
    terminal_write(", confirmed: ");
    terminal_write_line(have_env && grubenv_get(&env, "boot_slot") != NULL ? grubenv_get(&env, "boot_slot") : "?");

    if (have_env && grubenv_get(&env, "trial") != NULL && grubenv_get(&env, "trial")[0] != '\0')
    {
        terminal_write("  trial at the next boot: ");
        terminal_write_line(grubenv_get(&env, "trial"));
    }
}

static void cmd_mount(
    int argc,
    char **argv)
{
    if (argc != 2)
    {
        terminal_write_line("usage: mount <device> (see disks)");
        return;
    }

    block_device_t *device = block_device_find(argv[1]);

    if (device == NULL)
        terminal_write_line("mount: no such device");
    else if (volume_mount_device(device) != 0)
        terminal_write_line("mount: no HFS volume on it");
    else
        terminal_write_line("mount: ok");
}

static void cmd_config(
    int argc,
    char **argv)
{
    const char *key;
    const char *value;

    (void)argc;
    (void)argv;

    if (config_entry(0, &key, &value) != 0)
    {
        terminal_write_line("No settings (no 68k_hal.cfg, nothing on the command line)");
        return;
    }

    for (int i = 0; config_entry(i, &key, &value) == 0; i++)
    {
        terminal_write(key);
        terminal_write(" = ");
        terminal_write_line(value);
    }
}

static void cmd_cache(
    int argc,
    char **argv)
{
    block_cache_stats_t st;

    if (argc >= 2 && k_strcmp(argv[1], "drop") == 0)
    {
        block_cache_drop();
        terminal_write_line("cache: dropped");
        return;
    }

    /* cache bench <device> [MB]: read the start of a device twice and time both (the second from the cache). */
    if (argc >= 3 && k_strcmp(argv[1], "bench") == 0)
    {
        static uint8_t chunk[64 * 1024];
        block_device_t *dev = block_device_find(argv[2]);
        size_t mb = 16;

        if (argc >= 4 && (parse_size(argv[3], &mb) != 0 || mb > 4096))
            mb = 16;

        if (dev == NULL || dev->block_size == 0 || sizeof chunk % dev->block_size != 0)
        {
            terminal_write_line("cache: no such device");
            return;
        }

        uint32_t per = (uint32_t)(sizeof chunk / dev->block_size);
        uint64_t blocks = (uint64_t)mb * 1024 * 1024 / dev->block_size;

        if (blocks > dev->block_count)
            blocks = dev->block_count;

        for (int pass = 1; pass <= 2; pass++)
        {
            uint64_t start = hal_timer_ns();

            for (uint64_t lba = 0; lba < blocks; lba += per)
            {
                uint32_t n = blocks - lba < per ? (uint32_t)(blocks - lba) : per;

                if (block_read(dev, lba, n, chunk) != 0)
                {
                    terminal_write_line("cache: read error");
                    return;
                }
            }

            write_number_line(pass == 1 ? "first read (ms):  " : "second read (ms): ",
                              (uint32_t)((hal_timer_ns() - start) / 1000000u), 0);
        }
        return;
    }

    block_cache_get_stats(&st);

    if (st.lines_max == 0)
    {
        terminal_write_line("cache: off (disk_cache = 0 or no memory)");
        return;
    }

    uint64_t total = st.hits + st.misses;

    write_number_line("lines in use (32 KiB each): ", st.lines_used, 0);
    write_number_line("lines at most:              ", st.lines_max, 0);
    write_number_line("hits:                       ", (uint32_t)st.hits, 0);
    write_number_line("misses (read from disk):    ", (uint32_t)st.misses, 0);
    write_number_line("hit rate (percent):         ", total != 0 ? (uint32_t)(st.hits * 100 / total) : 0, 0);
    write_number_line("evictions:                  ", (uint32_t)st.evictions, 0);
    write_number_line("writes (written through):   ", (uint32_t)st.writes, 0);
}

int shell_command_count(void)
{
    return COMMAND_TABLE_SIZE;
}

const char *shell_command_name(int index)
{
    return commands[index].name;
}
