/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "emu/m68k/vm.h"

#include <stddef.h>

#include "emu/m68k/trap.h"
#include "kernel/lib/k_memory.h"
#include "third_party/musashi/m68k.h"

/* STOP #imm opcode (M68000 Programmer's Reference Manual, STOP). */
#define OPCODE_STOP 0x4E72

/* A-line opcodes are $Axxx (M68000 Programmer's Reference Manual, 8.2). */
#define OPCODE_LINE_MASK 0xF000
#define OPCODE_LINE_A 0xA000

#define MAX_TRAP_CHAIN 65536

static uint8_t *vm_ram;
static uint32_t vm_ram_size;
static bool vm_halted;
static bool vm_core_initialized;
static m68k_vm_write_watch_t vm_write_watch;


uint32_t m68k_vm_read8(uint32_t addr)
{
    if (addr >= vm_ram_size)
    {
        return 0;
    }
    return vm_ram[addr];
}


uint32_t m68k_vm_read16(uint32_t addr)
{
    return (m68k_vm_read8(addr) << 8) | m68k_vm_read8(addr + 1);
}


uint32_t m68k_vm_read32(uint32_t addr)
{
    return (m68k_vm_read16(addr) << 16) | m68k_vm_read16(addr + 2);
}


void m68k_vm_write8(uint32_t addr, uint32_t value)
{
    if (addr < vm_ram_size)
    {
        vm_ram[addr] = (uint8_t)value;
        if (vm_write_watch != NULL)
        {
            vm_write_watch(addr);
        }
    }
}


void m68k_vm_write16(uint32_t addr, uint32_t value)
{
    m68k_vm_write8(addr, value >> 8);
    m68k_vm_write8(addr + 1, value);
}


void m68k_vm_write32(uint32_t addr, uint32_t value)
{
    m68k_vm_write16(addr, value >> 16);
    m68k_vm_write16(addr + 2, value);
}


/* Memory callbacks required by Musashi. */
unsigned int m68k_read_memory_8(unsigned int address)
{
    return m68k_vm_read8(address);
}


unsigned int m68k_read_memory_16(unsigned int address)
{
    return m68k_vm_read16(address);
}


unsigned int m68k_read_memory_32(unsigned int address)
{
    return m68k_vm_read32(address);
}


void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    m68k_vm_write8(address, value);
}


void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    m68k_vm_write16(address, value);
}


void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    m68k_vm_write32(address, value);
}


/*
 * Called by Musashi before every instruction (see m68kconf.h).
 *
 * A-line opcodes are handled here, in a loop, because Musashi would execute
 * whatever sits at the new PC (e.g. a STOP right after a trap) without
 * calling the hook again. The cap keeps a handler that keeps redirecting PC
 * back to a trap from hanging the host; after it, Musashi takes the normal
 * line-A exception.
 */
void m68k_vm_instr_hook(unsigned int pc)
{
    uint16_t opcode = (uint16_t)m68k_vm_read16(pc);

    for (uint32_t i = 0;
         (opcode & OPCODE_LINE_MASK) == OPCODE_LINE_A && i < MAX_TRAP_CHAIN;
         i++)
    {
        /* Skip the opcode first so a handler can redirect PC. */
        m68k_set_reg(M68K_REG_PC, pc + 2);
        m68k_trap_dispatch(opcode, pc);
        pc = m68k_get_reg(NULL, M68K_REG_PC);
        opcode = (uint16_t)m68k_vm_read16(pc);
    }

    if (opcode == OPCODE_STOP)
    {
        /* Let STOP execute, then leave the run loop. */
        vm_halted = true;
        m68k_end_timeslice();
    }
}


void m68k_vm_set_write_watch(m68k_vm_write_watch_t watch)
{
    vm_write_watch = watch;
}


int m68k_vm_init(uint8_t *ram, uint32_t size, m68k_vm_cpu_t cpu)
{
    if (ram == NULL || size < 8)
    {
        return -1;
    }

    vm_ram = ram;
    vm_ram_size = size;
    vm_halted = false;
    m68k_trap_init();

    if (!vm_core_initialized)
    {
        m68k_init();
        vm_core_initialized = true;
    }

    m68k_set_cpu_type(cpu == M68K_VM_CPU_68030 ? M68K_CPU_TYPE_68030
                                               : M68K_CPU_TYPE_68000);
    return 0;
}


int m68k_vm_load(uint32_t addr, const void *data, uint32_t len)
{
    if (addr > vm_ram_size || len > vm_ram_size - addr)
    {
        return -1;
    }
    k_memcpy(vm_ram + addr, data, len);
    return 0;
}


int m68k_vm_reset(uint32_t entry, uint32_t sp)
{
    if (vm_ram == NULL)
    {
        return -1;
    }

    m68k_vm_write32(0, sp);
    m68k_vm_write32(4, entry);
    vm_halted = false;
    m68k_trap_clear_log();
    m68k_pulse_reset();
    /* Musashi charges the reset cycles to the next m68k_execute() call and
     * runs no instruction in it; get that over with now. */
    m68k_execute(1);
    return 0;
}


int m68k_vm_run(int cycles)
{
    int used = 0;

    while (!vm_halted && used < cycles)
    {
        used += m68k_execute(cycles - used);
    }
    return used;
}


bool m68k_vm_halted(void)
{
    return vm_halted;
}


static m68k_register_t to_musashi_reg(m68k_vm_reg_t reg)
{
    if (reg <= M68K_VM_REG_D0 + 7)
    {
        return (m68k_register_t)(M68K_REG_D0 + (reg - M68K_VM_REG_D0));
    }
    if (reg <= M68K_VM_REG_A0 + 7)
    {
        return (m68k_register_t)(M68K_REG_A0 + (reg - M68K_VM_REG_A0));
    }
    switch (reg)
    {
    case M68K_VM_REG_PC:
        return M68K_REG_PC;
    case M68K_VM_REG_SR:
        return M68K_REG_SR;
    case M68K_VM_REG_USP:
        return M68K_REG_USP;
    default:
        return M68K_REG_ISP;
    }
}


uint32_t m68k_vm_get_reg(m68k_vm_reg_t reg)
{
    return m68k_get_reg(NULL, to_musashi_reg(reg));
}


void m68k_vm_set_reg(m68k_vm_reg_t reg, uint32_t value)
{
    m68k_set_reg(to_musashi_reg(reg), value);
}
