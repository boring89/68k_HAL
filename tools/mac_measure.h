/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TOOLS_MAC_MEASURE_H
#define TOOLS_MAC_MEASURE_H

#include <stdint.h>
#include <stdio.h>

#include "emu/mac/mac.h"

/*
 * tools/mac_run -H: measurements for a 2x HiDPI Mac screen (docs/hidpi.md). Which traps the Mac OS
 * calls, which writes to the video RAM happen outside QuickDraw, whether ROM code enters QuickDraw
 * routines without the trap dispatcher, and which fonts the text traps draw with. Host only.
 */

/* Start measuring: installs the video RAM hook on mac. */
void measure_start(mac_t *mac);

/* Call before every instruction (from the pc hook). */
void measure_insn(mac_t *mac, uint32_t pc);

/* Call at the end of every frame. */
void measure_frame(mac_t *mac);

/* Write the report. */
void measure_report(mac_t *mac, FILE *out);

#endif
