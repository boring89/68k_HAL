/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_SERIAL_SERIAL_H
#define DRIVERS_SERIAL_SERIAL_H

#include <stdint.h>

#include "kernel/console/sink.h"

/* COM1, 38400 baud, 8N1, FIFO on, interrupts off (OSDev Wiki "Serial Ports"). */
void serial_init(void);

/* Returns 1, or 0 if the UART did not become ready in time. */
int serial_write_char(char c);

/* Stops at the first character that times out. */
void serial_write(const char *str);

/* Console output through COM1; serial_init must have run. */
extern const console_sink_t serial_console_sink;

#endif