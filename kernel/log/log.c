/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "log.h"
#include "../console/console.h"

void log_info(const char *msg)
{
    console_write_color(
        "[INFO] ",
        VGA_GREEN,
        VGA_BLACK
    );

    console_write_line(msg);
}

void log_debug(const char *msg)
{
    console_write_color(
        "[DEBUG] ",
        VGA_CYAN,
        VGA_BLACK
    );

    console_write_line(msg);
}

void log_warn(const char *msg)
{
    console_write_color(
        "[WARN] ",
        VGA_YELLOW,
        VGA_BLACK
    );

    console_write_line(msg);
}

void log_error(const char *msg)
{
    console_write_color(
        "[ERROR] ",
        VGA_LIGHT_RED,
        VGA_BLACK
    );

    console_write_line(msg);
}

void log_fatal(const char *msg)
{
    console_write_color(
        "[FATAL] ",
        VGA_RED,
        VGA_BLACK
    );

    console_write_line(msg);
}

void kernel_log(log_level_t lvl, const char *msg)
{

    if (!msg)
        msg = "NULL";

    switch (lvl)
    {
    case LOG_INFO:
        log_info(msg);
        break;
    case LOG_DEBUG:
        log_debug(msg);
        break;
    case LOG_WARN:
        log_warn(msg);
        break;
    case LOG_ERROR:
        log_error(msg);
        break;
    case LOG_FATAL:
        log_fatal(msg);
        break;
    
    default:
        log_error("Unknown log level");
        break;
    }
}