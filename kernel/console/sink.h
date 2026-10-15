/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_CONSOLE_SINK_H
#define KERNEL_CONSOLE_SINK_H

/*
 * A plain text output that receives a copy of everything written to
 * the console (e.g. a serial port). Implemented by drivers, registered
 * by the platform. Colours and clearing only apply to the terminal.
 */
typedef struct
{
    void (*write)(const char *str);
    void (*write_char)(char c);
} console_sink_t;

#define CONSOLE_MAX_SINKS 4

/*
 * Add sink; it must stay valid forever. Returns 0, or -1 if sink or one
 * of its functions is NULL, it is already registered, or the table is
 * full.
 */
int console_register_sink(const console_sink_t *sink);

/* Forget every sink. */
void console_sinks_reset(void);

/* Pass str or c to every sink, in registration order. */
void console_sinks_write(const char *str);
void console_sinks_write_char(char c);

#endif
