/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "emu/mac/mac.h"

#include <stddef.h>

#include "emu/m68k/vm.h"
#include "kernel/lib/k_memory.h"

/*
 * VIA1 (MOS 6522) register numbers; the registers sit 0x200 bytes apart
 * (Guide to the Macintosh Family Hardware, "VIA registers").
 */
#define VIA_ORB 0
#define VIA_ORA 1
#define VIA_DDRB 2
#define VIA_DDRA 3
#define VIA_ORA_NH 15

#define VIA1_BASE 0x50F00000u
#define VIA_SPAN 0x2000u
#define VIA_REG_SHIFT 9

/* Port A bit 4 of VIA1 is the ROM overlay line (vOverlay): the ROM maps
 * itself to address 0 until software drives that pin low. */
#define VIA1_A_OVERLAY 0x10


static uint32_t bswap_read(const uint8_t *p, int size)
{
    uint32_t v = 0;

    for (int i = 0; i < size; i++)
    {
        v = (v << 8) | p[i];
    }
    return v;
}


static void bswap_write(uint8_t *p, int size, uint32_t value)
{
    for (int i = size - 1; i >= 0; i--)
    {
        p[i] = (uint8_t)value;
        value >>= 8;
    }
}


static void log_unimpl(mac_t *mac, uint32_t addr, int size, bool write,
                       uint32_t value)
{
    mac_access_t *a = &mac->unimpl[mac->unimpl_total % MAC_UNIMPL_LOG_SIZE];

    a->addr = addr;
    a->pc = m68k_vm_get_reg(M68K_VM_REG_PC);
    a->value = value;
    a->size = (uint8_t)size;
    a->write = write;
    mac->unimpl_total++;
}


static void via1_update_overlay(mac_t *mac)
{
    /* The line is pulled up; it only goes low when driven as an output. */
    bool driven_low = (mac->via1_reg[VIA_DDRA] & VIA1_A_OVERLAY) &&
                      !(mac->via1_reg[VIA_ORA] & VIA1_A_OVERLAY);

    mac->overlay = !driven_low;
}


/* Byte access to the I/O area. Returns the read value. */
static uint32_t io_access(mac_t *mac, uint32_t addr, bool write, uint32_t value)
{
    if (addr >= VIA1_BASE && addr < VIA1_BASE + VIA_SPAN)
    {
        uint32_t reg = (addr - VIA1_BASE) >> VIA_REG_SHIFT;

        if (reg == VIA_ORA_NH)
        {
            reg = VIA_ORA;
        }
        if (write)
        {
            mac->via1_reg[reg] = value & 0xFF;
            via1_update_overlay(mac);
            return 0;
        }
        if (reg == VIA_ORA)
        {
            /* Output pins read back what was written, inputs read high. */
            return ((mac->via1_reg[VIA_ORA] & mac->via1_reg[VIA_DDRA]) |
                    ~mac->via1_reg[VIA_DDRA]) & 0xFF;
        }
        return mac->via1_reg[reg];
    }

    log_unimpl(mac, addr, 1, write, value);
    return 0;
}


uint32_t mac_bus_read(mac_t *mac, uint32_t addr, int size)
{
    if (addr >= MAC_ROM_BASE && addr < MAC_ROM_END)
    {
        uint32_t off = (addr - MAC_ROM_BASE) & (mac->rom_size - 1);

        if (off + size > mac->rom_size)
        {
            return 0;
        }
        return bswap_read(mac->rom + off, size);
    }

    if (addr < MAC_ROM_BASE)
    {
        if (mac->overlay)
        {
            uint32_t off = addr & (mac->rom_size - 1);

            return off + size > mac->rom_size ? 0
                                              : bswap_read(mac->rom + off, size);
        }
        if (addr + size <= mac->ram_size && addr + size >= addr)
        {
            return bswap_read(mac->ram + addr, size);
        }
        return 0;
    }

    if (addr >= MAC_IO_BASE && addr < MAC_IO_END)
    {
        uint32_t v = 0;

        for (int i = 0; i < size; i++)
        {
            v = (v << 8) | (io_access(mac, addr + i, false, 0) & 0xFF);
        }
        return v;
    }

    log_unimpl(mac, addr, size, false, 0);
    return 0;
}


void mac_bus_write(mac_t *mac, uint32_t addr, int size, uint32_t value)
{
    if (addr < MAC_ROM_BASE)
    {
        if (mac->overlay)
        {
            return;     /* ROM is read-only */
        }
        if (addr + size <= mac->ram_size && addr + size >= addr)
        {
            bswap_write(mac->ram + addr, size, value);
        }
        return;
    }

    if (addr >= MAC_ROM_BASE && addr < MAC_ROM_END)
    {
        return;
    }

    if (addr >= MAC_IO_BASE && addr < MAC_IO_END)
    {
        for (int i = 0; i < size; i++)
        {
            uint32_t shift = (size - 1 - i) * 8;

            io_access(mac, addr + i, true, (value >> shift) & 0xFF);
        }
        return;
    }

    log_unimpl(mac, addr, size, true, value);
}


static uint32_t bus_read(void *ctx, uint32_t addr, int size)
{
    return mac_bus_read(ctx, addr, size);
}


static void bus_write(void *ctx, uint32_t addr, int size, uint32_t value)
{
    mac_bus_write(ctx, addr, size, value);
}


static void insn_hook(void *ctx, uint32_t pc)
{
    mac_t *mac = ctx;

    mac->insn_count++;
    if (mac->trace_enabled)
    {
        mac_trace_t *t = &mac->trace[mac->trace_next % MAC_TRACE_SIZE];

        t->pc = pc;
        t->opcode = (uint16_t)m68k_vm_read16(pc);
        mac->trace_next++;
    }
}


int mac_init(mac_t *mac, uint8_t *ram, uint32_t ram_size,
             const uint8_t *rom, uint32_t rom_size)
{
    static const m68k_vm_bus_t bus_template = { bus_read, bus_write, NULL };
    m68k_vm_bus_t bus = bus_template;

    if (mac == NULL || ram == NULL || rom == NULL || ram_size < 8 ||
        rom_size < 0x10000 || (rom_size & (rom_size - 1)) != 0)
    {
        return -1;
    }

    k_memset(mac, 0, sizeof *mac);
    mac->ram = ram;
    mac->ram_size = ram_size;
    mac->rom = rom;
    mac->rom_size = rom_size;

    if (m68k_vm_init(ram, ram_size, M68K_VM_CPU_68030) != 0)
    {
        return -1;
    }
    bus.ctx = mac;
    m68k_vm_set_bus(&bus);
    m68k_vm_set_insn_hook(insn_hook, mac);
    return 0;
}


int mac_reset(mac_t *mac)
{
    k_memset(mac->via1_reg, 0, sizeof mac->via1_reg);
    mac->overlay = true;
    mac->insn_count = 0;
    mac->trace_next = 0;
    mac->unimpl_total = 0;
    m68k_vm_reset_cpu();
    mac->insn_count = 0;    /* the reset itself runs one instruction */
    return 0;
}


int mac_run(mac_t *mac, int cycles)
{
    (void)mac;
    return m68k_vm_run(cycles);
}


uint32_t mac_unimpl_total(const mac_t *mac)
{
    return mac->unimpl_total;
}
