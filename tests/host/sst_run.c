/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Runs SingleStepTests 68000 cases (converted by tools/sst_convert.py) on the
 * Musashi core in 68000 mode. Not part of "make test"; see "make test-sst".
 *
 * Usage: sst_run <label> < converted-tests
 *
 * Each test sets the registers and memory, executes one instruction and
 * compares registers, PC and memory with the expected final state. Prints one
 * summary line, plus the first few failures. Always exits 0: the numbers are
 * the result, and Musashi differs from the reference in some corner cases.
 * Cycle counts, bus transactions and the prefetch queue are not compared.
 *
 * Tests that end in an address error (vector 3) are counted separately. The
 * reference raises the error when the instruction or its operands are
 * fetched, as part of the same instruction, and fills the unused high bits
 * of the stacked status word from the opcode. Musashi has no prefetch queue:
 * it raises the error for an odd PC only when the next instruction is
 * fetched, and leaves those bits zero. So most such tests differ by design.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu/m68k/vm.h"

#define RAM_SIZE (16u * 1024 * 1024)    /* 24-bit address space of the 68000 */
#define MAX_RAM_ENTRIES 64
#define MAX_REPORTED_FAILURES 3
#define LINE_SIZE 65536

#define REG_COUNT 19                    /* d0-d7 a0-a6 usp ssp sr pc */
#define IDX_USP 15
#define IDX_SSP 16
#define IDX_SR 17
#define IDX_PC 18

typedef struct
{
    uint32_t reg[REG_COUNT];
    uint32_t prefetch[2];
    uint32_t ram_count;
    uint32_t ram_addr[MAX_RAM_ENTRIES];
    uint8_t ram_val[MAX_RAM_ENTRIES];
} state_t;

static const char *const reg_names[REG_COUNT] = {
    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "usp", "ssp", "sr", "pc",
};

static uint8_t ram[RAM_SIZE];

/* Every byte the CPU wrote during the current test. */
#define MAX_WRITES 4096
static uint32_t written[MAX_WRITES];
static uint32_t written_count;
static uint32_t write_overflow;


static void on_write(uint32_t addr)
{
    if (written_count < MAX_WRITES)
    {
        written[written_count++] = addr;
    }
    else
    {
        write_overflow++;
    }
}


static char *cursor;

static uint32_t next_number(void)
{
    return (uint32_t)strtoul(cursor, &cursor, 10);
}


static int parse_state(state_t *s)
{
    for (int i = 0; i < REG_COUNT; i++)
    {
        s->reg[i] = next_number();
    }
    s->prefetch[0] = next_number();
    s->prefetch[1] = next_number();
    s->ram_count = next_number();
    if (s->ram_count > MAX_RAM_ENTRIES)
    {
        return -1;
    }
    for (uint32_t i = 0; i < s->ram_count; i++)
    {
        s->ram_addr[i] = next_number();
        s->ram_val[i] = (uint8_t)next_number();
    }
    return 0;
}


static void set_state(const state_t *s)
{
    /*
     * Registers first in supervisor mode so that USP and ISP can be set
     * directly; setting SR last switches to the right stack.
     */
    m68k_vm_set_reg(M68K_VM_REG_SR, 0x2700);
    m68k_vm_set_reg(M68K_VM_REG_ISP, s->reg[IDX_SSP]);
    m68k_vm_set_reg(M68K_VM_REG_USP, s->reg[IDX_USP]);
    for (int i = 0; i < 15; i++)
    {
        m68k_vm_set_reg((m68k_vm_reg_t)i, s->reg[i]);
    }
    m68k_vm_set_reg(M68K_VM_REG_SR, s->reg[IDX_SR]);
    m68k_vm_set_reg(M68K_VM_REG_PC, s->reg[IDX_PC]);

    /* The prefetch queue holds the first two words at PC. */
    m68k_vm_write16(s->reg[IDX_PC], s->prefetch[0]);
    m68k_vm_write16(s->reg[IDX_PC] + 2, s->prefetch[1]);
    for (uint32_t i = 0; i < s->ram_count; i++)
    {
        ram[s->ram_addr[i]] = s->ram_val[i];
    }
}


static uint32_t get_reg(int i)
{
    switch (i)
    {
    case IDX_USP:
        return m68k_vm_get_reg(M68K_VM_REG_USP);
    case IDX_SSP:
        return m68k_vm_get_reg(M68K_VM_REG_ISP);
    case IDX_SR:
        return m68k_vm_get_reg(M68K_VM_REG_SR);
    case IDX_PC:
        return m68k_vm_get_reg(M68K_VM_REG_PC);
    default:
        return m68k_vm_get_reg((m68k_vm_reg_t)i);
    }
}


static int find_ram(const state_t *s, uint32_t addr)
{
    for (uint32_t i = 0; i < s->ram_count; i++)
    {
        if (s->ram_addr[i] == addr)
        {
            return (int)i;
        }
    }
    return -1;
}


/* Compare against the final state; fills 'why' and returns 1 on mismatch. */
static int compare(const state_t *init, const state_t *fin,
                   char *why, size_t why_size)
{
    for (int i = 0; i < REG_COUNT; i++)
    {
        uint32_t got = get_reg(i);

        if (got != fin->reg[i])
        {
            snprintf(why, why_size, "%s: expected %08x, got %08x",
                     reg_names[i], fin->reg[i], got);
            return 1;
        }
    }
    for (uint32_t i = 0; i < fin->ram_count; i++)
    {
        if (ram[fin->ram_addr[i]] != fin->ram_val[i])
        {
            snprintf(why, why_size, "ram[%06x]: expected %02x, got %02x",
                     fin->ram_addr[i], fin->ram_val[i], ram[fin->ram_addr[i]]);
            return 1;
        }
    }
    /* Bytes the CPU wrote that the expected state does not mention. */
    for (uint32_t i = 0; i < written_count; i++)
    {
        uint32_t addr = written[i];
        int in_fin = find_ram(fin, addr);
        int in_init = find_ram(init, addr);
        uint8_t before = in_init >= 0 ? init->ram_val[in_init] : 0;

        if (in_fin < 0 && ram[addr] != before)
        {
            snprintf(why, why_size, "unexpected write ram[%06x]: %02x",
                     addr, ram[addr]);
            return 1;
        }
    }
    return 0;
}


#define VECTOR_ADDRESS_ERROR 3

/* True if the expected final state is the start of the address error handler. */
static int expects_address_error(const state_t *init, const state_t *fin)
{
    uint32_t vector = 0;

    for (uint32_t i = 0; i < 4; i++)
    {
        int idx = find_ram(init, VECTOR_ADDRESS_ERROR * 4 + i);

        if (idx < 0)
        {
            return 0;
        }
        vector = (vector << 8) | init->ram_val[idx];
    }
    return vector == fin->reg[IDX_PC] && vector != init->reg[IDX_PC];
}


/* Put back every byte this test touched so the next one starts clean. */
static void clean_memory(const state_t *init, const state_t *fin)
{
    for (uint32_t i = 0; i < init->ram_count; i++)
    {
        ram[init->ram_addr[i]] = 0;
    }
    for (uint32_t i = 0; i < fin->ram_count; i++)
    {
        ram[fin->ram_addr[i]] = 0;
    }
    for (uint32_t i = 0; i < written_count; i++)
    {
        ram[written[i]] = 0;
    }
    ram[init->reg[IDX_PC] & (RAM_SIZE - 1)] = 0;
    for (uint32_t i = 0; i < 4; i++)
    {
        ram[(init->reg[IDX_PC] + i) & (RAM_SIZE - 1)] = 0;
    }
}


int main(int argc, char **argv)
{
    static char line[LINE_SIZE];
    static state_t init, fin;
    const char *label = argc > 1 ? argv[1] : "sst";
    uint32_t total = 0;
    uint32_t passed = 0;
    uint32_t reported = 0;
    uint32_t aerr_total = 0;
    uint32_t aerr_passed = 0;

    if (m68k_vm_init(ram, RAM_SIZE, M68K_VM_CPU_68000) != 0 ||
        m68k_vm_reset(0, 0) != 0)
    {
        fprintf(stderr, "%s: cannot start the VM\n", label);
        return 1;
    }
    m68k_vm_set_write_watch(on_write);

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        char *name_end = strchr(line, ' ');
        char why[160];

        if (name_end == NULL)
        {
            continue;
        }
        *name_end = '\0';
        cursor = name_end + 1;
        if (parse_state(&init) != 0 || parse_state(&fin) != 0)
        {
            fprintf(stderr, "%s: bad test line %s\n", label, line);
            continue;
        }

        written_count = 0;
        write_overflow = 0;
        set_state(&init);
        /* Setup writes are not the CPU's; only watch the instruction. */
        written_count = 0;
        m68k_vm_run(1);
        total++;

        int aerr = expects_address_error(&init, &fin);
        int ok = compare(&init, &fin, why, sizeof(why)) == 0 &&
                 write_overflow == 0;

        aerr_total += aerr;
        if (ok)
        {
            passed++;
            aerr_passed += aerr;
        }
        else if (!aerr && reported < MAX_REPORTED_FAILURES)
        {
            reported++;
            printf("  FAIL %s: %s\n", line, why);
        }
        clean_memory(&init, &fin);
    }

    uint32_t plain_total = total - aerr_total;
    uint32_t plain_passed = passed - aerr_passed;

    printf("%-16s %6u / %6u passed (%5.1f%%)  without address errors: "
           "%6u / %6u (%5.1f%%)\n", label, plain_passed + aerr_passed, total,
           total ? 100.0 * passed / total : 0.0, plain_passed, plain_total,
           plain_total ? 100.0 * plain_passed / plain_total : 100.0);
    return 0;
}
