/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_PS2_KEYBOARD_PS2_KEYBOARD_H
#define DRIVERS_PS2_KEYBOARD_PS2_KEYBOARD_H

/*
 * Start the PS/2 keyboard on IRQ 1. Key presses and releases are pushed
 * to the input event buffer (kernel/input) from the interrupt handler.
 */
void ps2_keyboard_init(void);


#endif