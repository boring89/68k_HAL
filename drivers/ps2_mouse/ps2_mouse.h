/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_PS2_MOUSE_PS2_MOUSE_H
#define DRIVERS_PS2_MOUSE_PS2_MOUSE_H

/*
 * Start the PS/2 mouse on IRQ 12 (the second channel of the 8042
 * controller). Movement and button reports are pushed to the input event
 * buffer as INPUT_EVENT_MOUSE from the interrupt handler. Call after the
 * interrupt controller is set up. Returns 0, or -1 if the controller or the
 * mouse does not answer (then IRQ 12 stays masked).
 */
int ps2_mouse_init(void);

#endif
