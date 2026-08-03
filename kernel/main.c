/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "console/console.h"
#include "log/log.h"
#include "printf/printf.h"

#include "interrupt/idt.h"
#include "interrupt/pic.h"

#include "timer/pit.h"
#include "keyboard/keyboard.h"

#include <stdint.h>

extern volatile uint8_t keyboard_last_scancode;

void kernel_main(void)
{
    console_init();

    kernel_log(LOG_INFO, "Kernel Booting...");

    idt_init();
    kernel_log(LOG_INFO, "IDT loaded");

    pic_init();
    kernel_log(LOG_INFO, "PIC initialized");

    keyboard_init();

    pit_init(100);

    asm volatile("sti");

    uint32_t last_tick = 0;

    while (1)
    {
        uint32_t tick = pit_get_ticks();

        if (tick - last_tick >= 100)
        {
            kernel_printf("1 second\n");
            last_tick = tick;
        }

        input_event_t event;

        if (keyboard_read(&event) == 0)
        {
            if (event.type == INPUT_EVENT_KEYBOARD)
            {
                if (event.data.keyboard.pressed)
                {
                    kernel_printf(
                        "%x\n",
                        event.data.keyboard.scancode);
                }
            }
        }

        asm volatile("hlt");
    }
}
