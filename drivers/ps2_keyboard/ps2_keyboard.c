/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ps2_keyboard.h"
#include "scancode.h"
#include "keymap.h"

#include "kernel/input/event.h"
#include "kernel/input/event_buffer.h"

#include "hal/irq.h"

#include "arch/x86/io/io.h"

/* 8042 controller data port and the keyboard's IRQ (OSDev Wiki "8042 PS/2 Controller"). */
#define PS2_DATA_PORT 0x60
#define PS2_KEYBOARD_IRQ 1

static void ps2_keyboard_handler(void)
{
    uint8_t scancode = inb(PS2_DATA_PORT);

    keyboard_key_event_t key;

    if (scancode_process(scancode, &key))
    {
        keycode_t keycode =
            keyboard_scancode_to_keycode(
                key.code);

        if (keycode != KEY_NONE)
        {
            input_event_t event =
                input_event_keyboard(
                    keycode,
                    key.pressed);

            input_event_push(&event);
        }
    }
}

void ps2_keyboard_init(void)
{
    scancode_init();

    hal_irq_register(PS2_KEYBOARD_IRQ, ps2_keyboard_handler);
    hal_irq_unmask(PS2_KEYBOARD_IRQ);
}