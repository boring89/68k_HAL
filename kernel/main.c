/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "kernel/console/console.h"
#include "kernel/terminal/terminal.h"
#include "kernel/terminal/input.h"
#include "kernel/shell/shell.h"

#include "kernel/log/log.h"
#include "kernel/printf/printf.h"

#include "kernel/interrupt/idt.h"
#include "kernel/interrupt/pic.h"
#include "kernel/interrupt/irq_callback.h"

#include "kernel/input/event_buffer.h"

#include "kernel/timer/pit.h"
#include "kernel/keyboard/keyboard.h"
#include "kernel/keyboard/state.h"
#include "kernel/keyboard/keychar.h"

#include "kernel/memory/memory.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/multiboot.h"
#include "kernel/memory/paging.h"

#include "kernel/panic/panic.h"

#ifdef KERNEL_SELFTEST
#include "kernel/test/selftest.h"
#endif

#include <stdint.h>

void kernel_main(
    uint32_t magic,
    uint32_t mbi_address)
{

    console_init();

    if (magic != 0x2BADB002)
    {
        kernel_log(LOG_ERROR, "Invalid Multiboot magic");

        while (1)
            asm volatile("hlt");
    }

    multiboot_init(mbi_address);

    kernel_log(LOG_INFO, "Multiboot initialized");

    const multiboot_info_t *mbi =
        multiboot_get_info();

    kernel_printf(
        "Multiboot flags: %x\n",
        mbi->flags);

    kernel_printf(
        "Multiboot info: %x\n",
        mbi_address);

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
    idt_init();
    kernel_log(LOG_INFO, "IDT loaded");

    /* After idt_init so a page fault is reported instead of triple faulting. */
    paging_init();
    kernel_log(LOG_INFO, "Paging enabled");

    pic_init();
    kernel_log(LOG_INFO, "PIC initialized");
    irq_callback_init();
    input_event_buffer_init();

    terminal_input_init();
    terminal_input_set_callback(shell_execute);
    shell_init();
    keyboard_init();
    keyboard_state_init();

    pit_init(100);

    asm volatile("sti");

#ifdef KERNEL_SELFTEST
    selftest_run();
#endif

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
                        terminal_input_char(c);
                    }
                }
            }
        }

        asm volatile("hlt");
    }
}



