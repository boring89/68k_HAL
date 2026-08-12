/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "console/console.h"
#include "terminal/terminal.h"
#include "terminal/input.h"
#include "shell/shell.h"

#include "log/log.h"
#include "printf/printf.h"

#include "interrupt/idt.h"
#include "interrupt/pic.h"
#include "interrupt/irq_callback.h"

#include "input/event_buffer.h"

#include "timer/pit.h"
#include "keyboard/keyboard.h"
#include "keyboard/state.h"
#include "keyboard/keychar.h"

#include "memory/memory.h"
#include "memory/heap.h"
#include "memory/multiboot.h"

#include "../emu/m68k/cpu.h"
#include "../emu/m68k/memory.h"

#include <stdint.h>

void m68k_emu_test(void)
{
    m68k_cpu_t cpu;

    kernel_printf("[68k] CPU test start\n");

    m68k_memory_init();
    m68k_cpu_init(&cpu);

    /*
     * MOVE.L #10, D0
     *
     * 0000: 203C
     * 0002: 0000000A
     */
    m68k_memory_write16(0x0000, 0x203C);
    m68k_memory_write32(0x0002, 10);

    /*
     * ADDI.L #5, D0
     *
     * 0006: 0680
     * 0008: 00000005
     */
    m68k_memory_write16(0x0006, 0x0680);
    m68k_memory_write32(0x0008, 5);

    kernel_printf(
        "[68k] Initial D0 = %u, PC = %u\n",
        cpu.d[0],
        cpu.pc
    );

    /*
     * MOVE.L #10,D0
     */
    if (m68k_cpu_step(&cpu) != 0)
    {
        kernel_printf("[68k] MOVE execution FAILED\n");
        return;
    }

    kernel_printf(
        "[68k] After MOVE: D0 = %u, PC = %u\n",
        cpu.d[0],
        cpu.pc
    );

    if (cpu.d[0] != 10)
    {
        kernel_printf(
            "[68k] FAIL: expected D0 = 10, got %u\n",
            cpu.d[0]
        );
        return;
    }

    /*
     * ADDI.L #5,D0
     */
    if (m68k_cpu_step(&cpu) != 0)
    {
        kernel_printf("[68k] ADD execution FAILED\n");
        return;
    }

    kernel_printf(
        "[68k] After ADD: D0 = %u, PC = %u\n",
        cpu.d[0],
        cpu.pc
    );

    if (cpu.d[0] != 15)
    {
        kernel_printf(
            "[68k] FAIL: expected D0 = 15, got %u\n",
            cpu.d[0]
        );
        return;
    }

    if (cpu.pc != 12)
    {
        kernel_printf(
            "[68k] FAIL: expected PC = 12, got %u\n",
            cpu.pc
        );
        return;
    }

    kernel_printf("[68k] CPU test PASSED\n");
}

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
    heap_init();
    kernel_printf("Kernel end: %x\n", memory_get_kernel_end());

    kernel_log(LOG_INFO, "Kernel Booting...");
    idt_init();
    kernel_log(LOG_INFO, "IDT loaded");

    pic_init();
    kernel_log(LOG_INFO, "PIC initialized");
    irq_callback_init();
    input_event_buffer_init();

    terminal_input_init();
    terminal_input_set_callback(shell_execute);
    shell_init();
    keyboard_init();
    keyboard_state_init();

    m68k_emu_test();

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
                        terminal_input_char(c);
                    }
                }
            }
        }

        asm volatile("hlt");
    }
}



