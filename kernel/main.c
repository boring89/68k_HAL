/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "main.h"

#include "kernel/console/console.h"
#include "kernel/terminal/terminal.h"
#include "kernel/terminal/input.h"
#include "kernel/shell/shell.h"

#include "kernel/log/log.h"
#include "kernel/printf/printf.h"

#include "kernel/input/event_buffer.h"

#include "kernel/keyboard/state.h"
#include "kernel/keyboard/keychar.h"

#include "kernel/memory/memory.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/pmm.h"

#include "kernel/panic/panic.h"

#include "hal/cpu.h"
#include "hal/init.h"
#include "hal/timer.h"

#ifdef KERNEL_SELFTEST
#include "kernel/test/selftest.h"
#endif

#include <stdint.h>

void kernel_main(void)
{
    hal_init_console();
    console_init();

    hal_init_early();

    memory_init();
    pmm_init(memory_get_kernel_start(), memory_get_kernel_end());

    /* Physically contiguous: there is no paging yet. */
    uintptr_t heap_region =
        pmm_alloc_frames(KERNEL_HEAP_SIZE / PMM_FRAME_SIZE);

    if (heap_region == 0)
        panic("PMM: cannot allocate %u bytes for the heap",
              (unsigned int)KERNEL_HEAP_SIZE);

    heap_init(heap_region, KERNEL_HEAP_SIZE);

    kernel_printf(
        "PMM: %u / %u frames free\n",
        (unsigned int)pmm_get_free_frames(),
        (unsigned int)pmm_get_total_frames());
    kernel_printf("Kernel end: %x\n", memory_get_kernel_end());

    kernel_log(LOG_INFO, "Kernel Booting...");
    hal_init();

    input_event_buffer_init();

    terminal_input_init();
    terminal_input_set_callback(shell_execute);
    shell_init();
    keyboard_state_init();

    hal_cpu_enable_interrupts();

#ifdef KERNEL_SELFTEST
    selftest_run();
#endif

    uint32_t last_tick = 0;

    while (1)
    {
        uint32_t tick = hal_timer_ticks();

        if (tick - last_tick >= hal_timer_frequency())
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
                        terminal_input_char(c);
                    }
                }
            }
        }

        hal_cpu_idle();
    }
}



