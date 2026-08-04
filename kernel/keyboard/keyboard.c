/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "keyboard.h"
#include "scancode.h"
#include "keymap.h"

#include "../input/event.h"
#include "../input/event_buffer.h"

#include "../interrupt/irq_callback.h"

#include "../../arch/x86/io/io.h"

void keyboard_init(void)
{
    scancode_init();

    irq_register(1, keyboard_handler);
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