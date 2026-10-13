/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "keyboard.h"
#include "scancode.h"
#include "keymap.h"

#include "kernel/input/event.h"
#include "kernel/input/event_buffer.h"

#include "hal/irq.h"

#include "arch/x86/io/io.h"

void keyboard_init(void)
{
    scancode_init();

    hal_irq_register(1, keyboard_handler);
    hal_irq_unmask(1);
}

void keyboard_handler(void)
{
    uint8_t scancode = inb(0x60);

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