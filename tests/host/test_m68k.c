/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "emu/m68k/cpu.h"
#include "emu/m68k/memory.h"

static void test_memory_big_endian(void)
{
    m68k_memory_init();

    m68k_memory_write32(0x100, 0x12345678);

    TEST_ASSERT_EQ_INT(0x12, m68k_memory_read8(0x100));
    TEST_ASSERT_EQ_INT(0x78, m68k_memory_read8(0x103));
    TEST_ASSERT_EQ_INT(0x1234, m68k_memory_read16(0x100));
    TEST_ASSERT_EQ_INT(0x12345678, m68k_memory_read32(0x100));
}

static void test_memory_out_of_range(void)
{
    m68k_memory_init();

    m68k_memory_write8(M68K_MEMORY_SIZE, 0xFF);

    TEST_ASSERT_EQ_INT(0, m68k_memory_read8(M68K_MEMORY_SIZE));
}

/* Moved from m68k_emu_test() in kernel/main.c. */
static void test_move_addi(void)
{
    m68k_cpu_t cpu;

    m68k_memory_init();
    m68k_cpu_init(&cpu);

    /* MOVE.L #10, D0 */
    m68k_memory_write16(0x0000, 0x203C);
    m68k_memory_write32(0x0002, 10);

    /* ADDI.L #5, D0 */
    m68k_memory_write16(0x0006, 0x0680);
    m68k_memory_write32(0x0008, 5);

    TEST_ASSERT_EQ_INT(0, m68k_cpu_step(&cpu));
    TEST_ASSERT_EQ_INT(10, cpu.d[0]);
    TEST_ASSERT_EQ_INT(6, cpu.pc);

    TEST_ASSERT_EQ_INT(0, m68k_cpu_step(&cpu));
    TEST_ASSERT_EQ_INT(15, cpu.d[0]);
    TEST_ASSERT_EQ_INT(12, cpu.pc);
}

static void test_invalid_opcode(void)
{
    m68k_cpu_t cpu;

    m68k_memory_init();
    m68k_cpu_init(&cpu);

    /* 0xFFFF is an F-line opcode, not supported. */
    m68k_memory_write16(0x0000, 0xFFFF);

    TEST_ASSERT_EQ_INT(-1, m68k_cpu_step(&cpu));
}

int main(void)
{
    TEST_RUN(test_memory_big_endian);
    TEST_RUN(test_memory_out_of_range);
    TEST_RUN(test_move_addi);
    TEST_RUN(test_invalid_opcode);

    return TEST_REPORT("m68k");
}
