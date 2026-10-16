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

#include "kernel/block/ramdisk.h"
#include "kernel/fs/volume.h"

#include "kernel/input/event_buffer.h"

#include "kernel/keyboard/state.h"
#include "kernel/keyboard/keychar.h"

#include "kernel/mac/machine.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/pmm.h"

#include "kernel/display/fb_pattern.h"
#include "kernel/panic/panic.h"

#include "hal/cpu.h"
#include "hal/framebuffer.h"
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

    kernel_printf(
        "PMM: %u / %u frames free\n",
        (unsigned int)pmm_get_free_frames(),
        (unsigned int)pmm_get_total_frames());
    kernel_printf("Kernel end: %x\n", memory_get_kernel_end());

    kernel_log(LOG_INFO, "Kernel Booting...");
    hal_init();

    /* Needs paging: the heap has its own virtual address range. */
    heap_vm_init();

    /* Boot modules (hal/boot.h) become block devices ram0, ram1, ... */
    ramdisk_register_boot_modules();

    /* The first HFS volume among them is what the ls and cat commands read. */
    if (volume_mount_any() == 0)
        kernel_log(LOG_INFO, "HFS volume mounted");

    /* Graphics mode has no text output yet: show a test pattern instead. */
    hal_framebuffer_t framebuffer;

    if (hal_framebuffer_get(&framebuffer) == 0)
    {
        kernel_printf("Framebuffer: %ux%u\n", framebuffer.width, framebuffer.height);
        fb_draw_test_pattern(&framebuffer);
    }

    input_event_buffer_init();

    terminal_input_init();
    terminal_input_set_callback(shell_execute);
    shell_init();
    keyboard_state_init();

    hal_cpu_enable_interrupts();

#ifdef KERNEL_MAC_TEST
    /* make test-rom-qemu: run the Mac for a fixed time and report the screen. */
    if (mac_machine_run(KERNEL_MAC_TEST_FRAMES) == 0)
        kernel_printf("mac: screen crc %x after %u frames\n",
                      mac_machine_screen_crc(),
                      (unsigned int)KERNEL_MAC_TEST_FRAMES);
    else
        kernel_printf("mac: could not run\n");
    while (1)
        hal_cpu_idle();
#endif

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



