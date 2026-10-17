/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_SHELL_COMMAND_H
#define KERNEL_SHELL_COMMAND_H

#include "parser.h"

typedef enum
{
    COMMAND_HELP,
    COMMAND_CLEAR,
    COMMAND_ECHO,
    COMMAND_TICKS,
    COMMAND_INFO,
    COMMAND_MEMORY,
    COMMAND_HEAP,
    COMMAND_ALLOC,
    COMMAND_FREE,
    COMMAND_MMAP,
    COMMAND_M68K,
    COMMAND_MODULES,
    COMMAND_DATE,
    COMMAND_DISKS,
    COMMAND_LS,
    COMMAND_CAT,
    COMMAND_MAC,
    COMMAND_CPUINFO,
    COMMAND_ACPI,
    COMMAND_RUN,
    COMMAND_LSPCI,
    COMMAND_NET,
    COMMAND_DHCP,
    COMMAND_PING,
    COMMAND_DNS,
    COMMAND_BEEP,
    COMMAND_VIDEO,
    COMMAND_HTTP,
    COMMAND_USB,
    COMMAND_CD,
    COMMAND_PWD,
    COMMAND_DMESG,
    COMMAND_DIAG,
    COMMAND_POWEROFF,
    COMMAND_REBOOT

} shell_command_id_t;


typedef void (*shell_command_handler_t)(
    int argc,
    char **argv
);

typedef struct
{
    shell_command_id_t id;

    const char *name;

    const char *description;

    shell_command_handler_t handler;

} shell_command_entry_t;

void shell_dispatch(shell_command_t *cmd);


/* The built-in commands, for completion. */
int shell_command_count(void);
const char *shell_command_name(int index);

/*
 * The hardware diagnostics (the `diag` command; run before the first prompt when the kernel
 * command line has "diag"): one page each for cpuinfo, acpi, lspci, usb, disks, net and the
 * boot messages, each held until a key is pressed or a few seconds pass, so that every page
 * can be photographed (docs/p520.md).
 */
void shell_run_diagnostics(void);

#endif
