/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "emu/m68k/trap.h"
#include "emu/m68k/vm.h"

#include <stdint.h>

/* Test programs are assembled from the .s files in tests/m68k by the Makefile. */
#define PROGRAM_DIR "build/m68k/"
#define PROGRAM_BASE 0x1000
#define RAM_SIZE 0x10000
#define STACK_TOP 0x8000
#define MAX_CYCLES 100000

static uint8_t ram[RAM_SIZE];

/* Load tests/m68k/<name>.s (assembled) and run it until STOP. */
static void run_program(const char *name, m68k_vm_cpu_t cpu)
{
    char path[128];
    uint8_t image[4096];
    size_t len;
    FILE *f;

    snprintf(path, sizeof(path), PROGRAM_DIR "%s.bin", name);
    f = fopen(path, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "  cannot open %s\n", path);
        test_failures++;
        return;
    }
    len = fread(image, 1, sizeof(image), f);
    fclose(f);

    memset(ram, 0, sizeof(ram));
    TEST_ASSERT_EQ_INT(0, m68k_vm_init(ram, sizeof(ram), cpu));
    TEST_ASSERT_EQ_INT(0, m68k_vm_load(PROGRAM_BASE, image, (uint32_t)len));
    TEST_ASSERT_EQ_INT(0, m68k_vm_reset(PROGRAM_BASE, STACK_TOP));
    m68k_vm_run(MAX_CYCLES);
    TEST_ASSERT(m68k_vm_halted());
}

static uint32_t d(int n)
{
    return m68k_vm_get_reg((m68k_vm_reg_t)(M68K_VM_REG_D0 + n));
}

static void test_memory_big_endian(void)
{
    memset(ram, 0, sizeof(ram));
    m68k_vm_init(ram, sizeof(ram), M68K_VM_CPU_68030);

    m68k_vm_write32(0x100, 0x12345678);

    TEST_ASSERT_EQ_INT(0x12, ram[0x100]);
    TEST_ASSERT_EQ_INT(0x78, ram[0x103]);
    TEST_ASSERT_EQ_INT(0x1234, m68k_vm_read16(0x100));
    TEST_ASSERT_EQ_INT(0x12345678, m68k_vm_read32(0x100));
}

static void test_memory_out_of_range(void)
{
    memset(ram, 0, sizeof(ram));
    m68k_vm_init(ram, sizeof(ram), M68K_VM_CPU_68030);

    m68k_vm_write8(RAM_SIZE, 0xFF);

    TEST_ASSERT_EQ_INT(0, m68k_vm_read8(RAM_SIZE));
    TEST_ASSERT_EQ_INT(-1, m68k_vm_load(RAM_SIZE - 1, "ab", 2));
}

static void test_init_rejects_bad_args(void)
{
    TEST_ASSERT_EQ_INT(-1, m68k_vm_init(NULL, 64, M68K_VM_CPU_68030));
    TEST_ASSERT_EQ_INT(-1, m68k_vm_init(ram, 4, M68K_VM_CPU_68030));
}

static void check_move(m68k_vm_cpu_t cpu)
{
    run_program("move", cpu);

    TEST_ASSERT_EQ_INT(0x12345678, d(0));
    TEST_ASSERT_EQ_INT(0xFFFFFF80, d(1));
    TEST_ASSERT_EQ_INT(0x1234FFEE, d(2));
    TEST_ASSERT_EQ_INT(0xAB, d(3));
    TEST_ASSERT_EQ_INT(0x12345678, d(4));
    TEST_ASSERT_EQ_INT(0x12, ram[0x2000]);
    TEST_ASSERT_EQ_INT(0x78, ram[0x2003]);
}

static void test_move_68030(void)
{
    check_move(M68K_VM_CPU_68030);
}

static void test_move_68000(void)
{
    check_move(M68K_VM_CPU_68000);
}

static void test_arith_ccr_flags(void)
{
    run_program("arith", M68K_VM_CPU_68030);

    /* CCR bits: X=0x10 N=0x08 Z=0x04 V=0x02 C=0x01 */
    TEST_ASSERT_EQ_INT(0x08 | 0x02, d(4) & 0x1F);
    TEST_ASSERT_EQ_INT(0x10 | 0x04 | 0x01, d(5) & 0x1F);
    TEST_ASSERT_EQ_INT(0x10 | 0x08 | 0x01, d(6) & 0x1F);
    /* CMP leaves X unchanged; the SUBQ above set it. */
    TEST_ASSERT_EQ_INT(0x10 | 0x04, d(7) & 0x1F);
}

static void test_branch_loop(void)
{
    run_program("loop", M68K_VM_CPU_68030);

    TEST_ASSERT_EQ_INT(55, d(0));
    TEST_ASSERT_EQ_INT(3, d(1));
    TEST_ASSERT_EQ_INT(1, d(2));
    TEST_ASSERT_EQ_INT(0, d(3));
}

static void test_subroutine_stack(void)
{
    run_program("subroutine", M68K_VM_CPU_68030);

    TEST_ASSERT_EQ_INT(42, d(0));
    TEST_ASSERT_EQ_INT(0x11111111, d(1));
    TEST_ASSERT_EQ_INT(STACK_TOP, m68k_vm_get_reg(M68K_VM_REG_A0 + 7));
}

static void test_divide_by_zero_exception(void)
{
    run_program("divzero", M68K_VM_CPU_68030);

    TEST_ASSERT_EQ_INT(0xDEAD, d(7));
    TEST_ASSERT_EQ_INT(7, d(0));
    TEST_ASSERT_EQ_INT(7, d(1));
}

static void test_aline_trap_logged(void)
{
    m68k_trap_record_t rec;

    run_program("aline", M68K_VM_CPU_68030);

    /* Execution continues after each trap. */
    TEST_ASSERT_EQ_INT(1, d(0));
    TEST_ASSERT_EQ_INT(2, d(1));
    TEST_ASSERT_EQ_INT(3, d(2));

    TEST_ASSERT_EQ_INT(2, m68k_trap_total());
    TEST_ASSERT_EQ_INT(0, m68k_trap_log_get(0, &rec));
    TEST_ASSERT_EQ_INT(0xA9F4, rec.opcode);
    TEST_ASSERT_EQ_INT(PROGRAM_BASE + 2, rec.pc);
    TEST_ASSERT_EQ_INT(0, m68k_trap_log_get(1, &rec));
    TEST_ASSERT_EQ_INT(0xA000, rec.opcode);
    TEST_ASSERT_EQ_INT(-1, m68k_trap_log_get(2, &rec));
}

static void handler_set_d5(uint16_t opcode, uint32_t pc)
{
    (void)pc;
    m68k_vm_set_reg(M68K_VM_REG_D0 + 5, opcode);
}

static void test_aline_handler_called(void)
{
    static const uint8_t code[] = {
        0xA9, 0xF4,                 /* A-line trap $A9F4 */
        0x4E, 0x72, 0x27, 0x00,     /* STOP #$2700 */
    };

    memset(ram, 0, sizeof(ram));
    m68k_vm_init(ram, sizeof(ram), M68K_VM_CPU_68030);
    TEST_ASSERT_EQ_INT(0, m68k_trap_set_handler(0x9F4, handler_set_d5));
    TEST_ASSERT_EQ_INT(-1, m68k_trap_set_handler(0x1000, handler_set_d5));
    m68k_vm_load(PROGRAM_BASE, code, sizeof(code));
    m68k_vm_reset(PROGRAM_BASE, STACK_TOP);
    m68k_vm_run(MAX_CYCLES);

    TEST_ASSERT_EQ_INT(0xA9F4, d(5));
    TEST_ASSERT(m68k_vm_halted());
}

static void test_trap_log_wraps(void)
{
    m68k_trap_record_t rec;

    m68k_trap_init();
    for (uint32_t i = 0; i < M68K_TRAP_LOG_SIZE + 3; i++)
    {
        m68k_trap_dispatch((uint16_t)(0xA000 + i), 0x100 + i);
    }

    TEST_ASSERT_EQ_INT(M68K_TRAP_LOG_SIZE + 3, m68k_trap_total());
    TEST_ASSERT_EQ_INT(M68K_TRAP_LOG_SIZE, m68k_trap_log_length());
    TEST_ASSERT_EQ_INT(0, m68k_trap_log_get(0, &rec));
    TEST_ASSERT_EQ_INT(0xA003, rec.opcode);
    TEST_ASSERT_EQ_INT(0, m68k_trap_log_get(M68K_TRAP_LOG_SIZE - 1, &rec));
    TEST_ASSERT_EQ_INT(0xA000 + M68K_TRAP_LOG_SIZE + 2, rec.opcode);
}

static void test_run_cycle_limit(void)
{
    /* BRA.B -2 (0x60FE) loops forever; run must return at the limit. */
    static const uint8_t spin[] = { 0x60, 0xFE };
    int used;

    memset(ram, 0, sizeof(ram));
    m68k_vm_init(ram, sizeof(ram), M68K_VM_CPU_68030);
    m68k_vm_load(PROGRAM_BASE, spin, sizeof(spin));
    m68k_vm_reset(PROGRAM_BASE, STACK_TOP);

    used = m68k_vm_run(1000);

    TEST_ASSERT(used >= 1000);
    TEST_ASSERT(!m68k_vm_halted());
}

int main(void)
{
    TEST_RUN(test_memory_big_endian);
    TEST_RUN(test_memory_out_of_range);
    TEST_RUN(test_init_rejects_bad_args);
    TEST_RUN(test_move_68030);
    TEST_RUN(test_move_68000);
    TEST_RUN(test_arith_ccr_flags);
    TEST_RUN(test_branch_loop);
    TEST_RUN(test_subroutine_stack);
    TEST_RUN(test_divide_by_zero_exception);
    TEST_RUN(test_aline_trap_logged);
    TEST_RUN(test_aline_handler_called);
    TEST_RUN(test_trap_log_wraps);
    TEST_RUN(test_run_cycle_limit);

    return TEST_REPORT("test_m68k");
}
