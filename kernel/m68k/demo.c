/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "demo.h"

#include "guest.h"

#include "emu/m68k/trap.h"
#include "emu/m68k/vm.h"
#include "kernel/lib/k_number.h"
#include "kernel/terminal/terminal.h"

#include <stdint.h>

#define DEMO_ADDRESS 0x1000
#define DEMO_STACK 0x8000
#define DEMO_MAX_CYCLES 100000

/* Assembled from:
 *     moveq #0,%d0 ; moveq #10,%d1
 *  1: add.l %d1,%d0 ; subq.l #1,%d1 ; bne 1b
 *     .short 0xA9F4
 *     stop #0x2700
 */
static const uint8_t demo_program[] = {
    0x70, 0x00,                 /* MOVEQ #0,D0 */
    0x72, 0x0A,                 /* MOVEQ #10,D1 */
    0xD0, 0x81,                 /* ADD.L D1,D0 */
    0x53, 0x81,                 /* SUBQ.L #1,D1 */
    0x66, 0x00, 0xFF, 0xFA,     /* BNE.W back to ADD.L */
    0xA9, 0xF4,                 /* A-line trap $A9F4 */
    0x4E, 0x72, 0x27, 0x00,     /* STOP #$2700 */
};


static void write_hex(uint32_t value, int digits)
{
    const char hex[] = "0123456789ABCDEF";
    char buffer[9];

    for (int i = digits - 1; i >= 0; i--)
    {
        buffer[i] = hex[value & 0xF];
        value >>= 4;
    }
    buffer[digits] = '\0';
    terminal_write(buffer);
}


static void write_decimal(unsigned int value)
{
    char buffer[16];

    k_utoa(value, buffer);
    terminal_write(buffer);
}


static void write_register(const char *name, m68k_vm_reg_t reg, int digits)
{
    terminal_write(name);
    terminal_put_char('=');
    write_hex(m68k_vm_get_reg(reg), digits);
}


void m68k_demo_run(void)
{
    if (m68k_guest_init() != 0)
    {
        terminal_write_line("m68k: cannot set up the guest memory");
        return;
    }

    m68k_vm_load(DEMO_ADDRESS, demo_program, sizeof(demo_program));
    m68k_vm_reset(DEMO_ADDRESS, DEMO_STACK);
    int cycles = m68k_vm_run(DEMO_MAX_CYCLES);

    terminal_write_line("m68k demo (68030): sum of 1..10");
    terminal_write("cycles: ");
    write_decimal((unsigned int)cycles);
    terminal_write(m68k_vm_halted() ? "  (stopped)" : "  (cycle limit)");
    terminal_newline();

    write_register("D0", M68K_VM_REG_D0, 8);
    terminal_put_char(' ');
    write_register("D1", M68K_VM_REG_D0 + 1, 8);
    terminal_put_char(' ');
    write_register("PC", M68K_VM_REG_PC, 8);
    terminal_put_char(' ');
    write_register("SR", M68K_VM_REG_SR, 4);
    terminal_newline();

    terminal_write("A-line traps: ");
    write_decimal(m68k_trap_total());
    terminal_newline();

    for (uint32_t i = 0; i < m68k_trap_log_length(); i++)
    {
        m68k_trap_record_t record;

        if (m68k_trap_log_get(i, &record) == 0)
        {
            terminal_write("  ");
            write_hex(record.opcode, 4);
            terminal_write(" at ");
            write_hex(record.pc, 8);
            terminal_newline();
        }
    }
}
