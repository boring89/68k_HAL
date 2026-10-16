/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Host runner for the Macintosh IIci model (emu/mac).
 *
 * Usage: mac_run [-n instructions] [-t N] <rom-file>
 *
 *   -n   run at most this many instructions (default 1000000)
 *   -t   print the last N executed instructions at the end (default 32)
 *
 * The ROM is read from the file and never copied anywhere. Prints the CPU
 * registers, the trace tail and the unimplemented hardware accesses, which is
 * what to look at when the ROM gets stuck.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "emu/m68k/vm.h"
#include "emu/mac/mac.h"
#include "third_party/musashi/m68k.h"

#define RAM_SIZE (8u * 1024 * 1024)

void m68k_vm_fatal(const char *message)
{
    fprintf(stderr, "fatal: %s\n", message);
    exit(2);
}


/* Callbacks of the Musashi disassembler (see third_party/musashi/m68k.h). */
unsigned int m68k_read_disassembler_16(unsigned int address)
{
    return m68k_vm_read16(address);
}


unsigned int m68k_read_disassembler_32(unsigned int address)
{
    return m68k_vm_read32(address);
}


static uint8_t *read_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *buf;

    if (f == NULL)
    {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(n);
    if (buf == NULL || fread(buf, 1, n, f) != (size_t)n)
    {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *size = (uint32_t)n;
    return buf;
}


static void print_regs(void)
{
    for (int i = 0; i < 8; i++)
    {
        printf("D%d=%08X A%d=%08X%s", i, m68k_vm_get_reg(M68K_VM_REG_D0 + i),
               i, m68k_vm_get_reg(M68K_VM_REG_A0 + i), i % 2 ? "\n" : "  ");
    }
    printf("PC=%08X SR=%04X\n", m68k_vm_get_reg(M68K_VM_REG_PC),
           m68k_vm_get_reg(M68K_VM_REG_SR));
}


int main(int argc, char **argv)
{
    static mac_t mac;
    uint32_t max_insns = 1000000;
    uint32_t tail = 32;
    const char *rom_path = NULL;
    uint32_t rom_size = 0;
    uint8_t *rom;
    uint8_t *ram;

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] == 'n' && i + 1 < argc)
        {
            max_insns = strtoul(argv[++i], NULL, 0);
        }
        else if (argv[i][0] == '-' && argv[i][1] == 't' && i + 1 < argc)
        {
            tail = strtoul(argv[++i], NULL, 0);
        }
        else
        {
            rom_path = argv[i];
        }
    }
    if (rom_path == NULL)
    {
        fprintf(stderr, "usage: mac_run [-n insns] [-t N] <rom-file>\n");
        return 1;
    }

    rom = read_file(rom_path, &rom_size);
    if (rom == NULL)
    {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 1;
    }
    ram = calloc(1, RAM_SIZE);
    if (ram == NULL || mac_init(&mac, ram, RAM_SIZE, rom, rom_size) != 0)
    {
        fprintf(stderr, "mac_init failed (ROM size %u)\n", rom_size);
        return 1;
    }
    mac.trace_enabled = true;
    mac_reset(&mac);

    while (mac.insn_count < max_insns && !m68k_vm_halted())
    {
        mac_run(&mac, 1000);
    }

    printf("instructions: %u%s\n", mac.insn_count,
           m68k_vm_halted() ? " (halted)" : "");
    print_regs();

    printf("-- last %u instructions --\n", tail);
    for (uint32_t n = tail; n > 0; n--)
    {
        uint32_t idx;

        if (n > mac.trace_next || n > MAC_TRACE_SIZE)
        {
            continue;
        }
        idx = (mac.trace_next - n) % MAC_TRACE_SIZE;
        char text[128];

        m68k_disassemble(text, mac.trace[idx].pc, M68K_CPU_TYPE_68030);
        printf("%08X: %s\n", mac.trace[idx].pc, text);
    }

    printf("-- unimplemented accesses: %u --\n", mac_unimpl_total(&mac));
    for (uint32_t i = 0; i < MAC_UNIMPL_LOG_SIZE && i < mac.unimpl_total; i++)
    {
        const mac_access_t *a = &mac.unimpl[i];

        printf("%s%d %08X pc=%08X val=%08X\n", a->write ? "W" : "R", a->size,
               a->addr, a->pc, a->value);
    }
    return 0;
}
