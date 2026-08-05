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

    COMMAND_COUNT

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


#endif
