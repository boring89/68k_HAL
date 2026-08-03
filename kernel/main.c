/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "console/console.h"
#include "log/log.h"
#include "printf/printf.h"

#include "interrupt/idt.h"
#include "interrupt/pic.h"

#include "input/event_buffer.h"

#include "timer/pit.h"
#include "keyboard/keyboard.h"
#include "keyboard/state.h"
#include "keyboard/keychar.h"

#include <stdint.h>

void kernel_main(void)
{
    console_init();

    kernel_log(LOG_INFO, "Kernel Booting...");

    idt_init();
    kernel_log(LOG_INFO, "IDT loaded");

    pic_init();
    kernel_log(LOG_INFO, "PIC initialized");

    input_event_buffer_init();

    keyboard_init();
    keyboard_state_init();

    pit_init(100);

    asm volatile("sti");

    uint32_t last_tick = 0;

    while (1)
    {
        uint32_t tick = pit_get_ticks();

        if (tick - last_tick >= 100)
        {
            // kernel_printf("1 second\n");
            last_tick = tick;
        }

        input_event_t event;

        while (input_event_pop(&event) == 0)
        {
            if (event.type == INPUT_EVENT_KEYBOARD)
            {
                keyboard_state_update(
                    event.data.keyboard.key,
                    event.data.keyboard.pressed);

                const keyboard_state_t *state =
                    keyboard_state_get();

                if (event.data.keyboard.pressed)
                {
                    char c =
                        keyboard_keycode_to_ascii(
                            event.data.keyboard.key,
                            state);

                    if (c)
                    {
                        kernel_printf("%c", c);
                    }
                }
            }
        }

        asm volatile("hlt");
    }
}
