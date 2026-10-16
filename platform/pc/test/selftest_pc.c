/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* PC platform self tests, run by selftest_run (-DKERNEL_SELFTEST builds only). */
#ifdef KERNEL_SELFTEST

#include "kernel/test/selftest.h"

#include "hal/cpu.h"
#include "hal/irq.h"
#include "hal/mmu.h"
#include "hal/timer.h"

#include "platform/pc/boot/multiboot.h"
#include "platform/pc/interrupt/pic.h"

#include "arch/x86/cpu/cpu.h"
#include "arch/x86/io/io.h"

#include <stddef.h>
#include <stdint.h>

/*
 * QEMU isa-debug-exit device (-device isa-debug-exit,iobase=0xf4).
 * Writing value v makes QEMU exit with status (v << 1) | 1, so
 * PASS exits with 33 and FAIL exits with 35.
 */
#define QEMU_EXIT_PORT 0xF4
#define QEMU_EXIT_PASS 0x10
#define QEMU_EXIT_FAIL 0x11

static void test_hal_timer_frequency(void)
{
    /* hal_init starts the PIT at 100 Hz. */
    SELFTEST_ASSERT(hal_timer_frequency() == 100);
}

/* The nanosecond clock must agree with the PIT ticks it was calibrated on. */
static void test_hal_timer_ns(void)
{
    uint32_t tick0 = hal_timer_ticks();
    uint64_t ns0 = hal_timer_ns();

    while (hal_timer_ticks() - tick0 < 11)
        hal_cpu_idle();

    uint64_t elapsed = hal_timer_ns() - ns0;

    /* 10 to 11 ticks = 100..110 ms (the first tick may be partial). */
    SELFTEST_ASSERT(elapsed >= 90000000u);
    SELFTEST_ASSERT(elapsed <= 130000000u);
    SELFTEST_ASSERT(hal_timer_ns() > ns0);
}

/* hal_init identity maps the Multiboot data for the shell's mmap. */
static void test_multiboot_data_mapped(void)
{
    const multiboot_info_t *mbi = multiboot_get_info();
    uintptr_t address = (uintptr_t)mbi;
    uintptr_t phys = 0;

    SELFTEST_ASSERT(hal_mmu_get_phys(address, &phys) == 0);
    SELFTEST_ASSERT(phys == address);
    SELFTEST_ASSERT(hal_mmu_get_phys(address + sizeof(*mbi) - 1, &phys) == 0);

    SELFTEST_ASSERT(mbi->flags & MULTIBOOT_INFO_MEM_MAP);

    uintptr_t mmap_end = (uintptr_t)mbi->mmap_addr + mbi->mmap_length;

    SELFTEST_ASSERT(hal_mmu_get_phys(mbi->mmap_addr, &phys) == 0);
    SELFTEST_ASSERT(phys == mbi->mmap_addr);
    SELFTEST_ASSERT(hal_mmu_get_phys(mmap_end - 1, &phys) == 0);

    /* Walk the map like the shell's mmap command does. */
    int entries = 0;

    for (const multiboot_mmap_entry_t *entry = multiboot_mmap_first();
         entry != 0; entry = multiboot_mmap_next(entry))
        entries++;

    SELFTEST_ASSERT(entries > 0);
}

static void unused_irq_handler(void)
{
}

static void test_hal_irq_register_rejects(void)
{
    SELFTEST_ASSERT(hal_irq_register(16, unused_irq_handler) == -1);
    SELFTEST_ASSERT(hal_irq_register(0xFFFFFFFFu, unused_irq_handler) == -1);
    SELFTEST_ASSERT(hal_irq_register(6, NULL) == -1);

    /* Line 0 already belongs to the PIT. */
    SELFTEST_ASSERT(hal_irq_register(0, unused_irq_handler) == -1);

    /* The PIT handler must still be the one running. */
    uint32_t start = hal_timer_ticks();
    hal_cpu_idle();
    SELFTEST_ASSERT(hal_timer_ticks() != start);
}

/* 8259A data ports; reading them returns the IMR (OSDev Wiki "8259 PIC"). */
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_DATA 0xA1

/*
 * pic_init masks everything; the PIT (0), keyboard (1) and mouse (12) unmask,
 * and the mouse is on the slave, so the cascade line (2) is open too.
 */
#define BOOT_MASTER_MASK 0xF8
#define BOOT_SLAVE_MASK 0xEF

static void test_pic_boot_mask(void)
{
    SELFTEST_ASSERT(inb(PIC_MASTER_DATA) == BOOT_MASTER_MASK);
    SELFTEST_ASSERT(inb(PIC_SLAVE_DATA) == BOOT_SLAVE_MASK);
}

/* The mouse initialised in hal_init and has its handler on IRQ 12. */
static void test_ps2_mouse_irq(void)
{
    SELFTEST_ASSERT((inb(PIC_SLAVE_DATA) & (1u << 4)) == 0);
    SELFTEST_ASSERT(hal_irq_register(12, unused_irq_handler) == -1);
}

static void test_hal_irq_unmask_rejects(void)
{
    SELFTEST_ASSERT(hal_irq_unmask(16) == -1);
    SELFTEST_ASSERT(hal_irq_unmask(0xFFFFFFFFu) == -1);

    SELFTEST_ASSERT(inb(PIC_MASTER_DATA) == BOOT_MASTER_MASK);
    SELFTEST_ASSERT(inb(PIC_SLAVE_DATA) == BOOT_SLAVE_MASK);
}

static void test_hal_irq_unmask_master(void)
{
    uint8_t master = inb(PIC_MASTER_DATA);
    uint8_t slave = inb(PIC_SLAVE_DATA);

    /* Nothing drives IRQ 5; interrupts stay off while it is unmasked. */
    hal_cpu_disable_interrupts();

    SELFTEST_ASSERT(hal_irq_unmask(5) == 0);
    SELFTEST_ASSERT(inb(PIC_MASTER_DATA) == (master & ~(1u << 5)));
    SELFTEST_ASSERT(inb(PIC_SLAVE_DATA) == slave);

    outb(PIC_MASTER_DATA, master);
    hal_cpu_enable_interrupts();
}

/*
 * A slave line also needs the cascade input (master line 2) unmasked. It is
 * already open here because of the mouse, so close it first.
 */
static void test_hal_irq_unmask_slave(void)
{
    uint8_t master = inb(PIC_MASTER_DATA);
    uint8_t slave = inb(PIC_SLAVE_DATA);

    hal_cpu_disable_interrupts();
    outb(PIC_MASTER_DATA, master | (1u << 2));

    SELFTEST_ASSERT(hal_irq_unmask(8) == 0);
    SELFTEST_ASSERT(inb(PIC_MASTER_DATA) == (master & ~(1u << 2)));
    SELFTEST_ASSERT(inb(PIC_SLAVE_DATA) == (slave & ~(1u << 0)));

    outb(PIC_SLAVE_DATA, slave);
    outb(PIC_MASTER_DATA, master);
    hal_cpu_enable_interrupts();
}

/*
 * Outside an IRQ handler no ISR bit is set, so IRQ 7 and 15 look exactly
 * like spurious ones; other lines never are.
 */
static void test_pic_spurious(void)
{
    hal_cpu_disable_interrupts();

    uint32_t before = pic_get_spurious_count();

    SELFTEST_ASSERT(pic_handle_spurious(0) == 0);
    SELFTEST_ASSERT(pic_handle_spurious(8) == 0);
    SELFTEST_ASSERT(pic_get_spurious_count() == before);

    SELFTEST_ASSERT(pic_handle_spurious(7) == 1);
    SELFTEST_ASSERT(pic_handle_spurious(15) == 1);
    SELFTEST_ASSERT(pic_get_spurious_count() == before + 2);

    /* Reading the ISR must not disturb the masks. */
    SELFTEST_ASSERT(inb(PIC_MASTER_DATA) == BOOT_MASTER_MASK);
    SELFTEST_ASSERT(inb(PIC_SLAVE_DATA) == BOOT_SLAVE_MASK);

    hal_cpu_enable_interrupts();
}

/* pic_unmask restores IF as it found it. */
static void test_pic_unmask_keeps_interrupt_flag(void)
{
    uint8_t master = inb(PIC_MASTER_DATA);

    hal_cpu_disable_interrupts();
    SELFTEST_ASSERT(hal_irq_unmask(5) == 0);
    SELFTEST_ASSERT((cpu_irq_save() & CPU_EFLAGS_IF) == 0);
    outb(PIC_MASTER_DATA, master);
    hal_cpu_enable_interrupts();

    uint32_t ticks = hal_timer_ticks();

    /* Interrupts are on again: the timer keeps advancing. */
    for (int i = 0; i < 1000 && hal_timer_ticks() == ticks; i++)
        hal_cpu_idle();

    SELFTEST_ASSERT(hal_timer_ticks() != ticks);
}

void selftest_platform_run(void)
{
    SELFTEST_RUN(test_hal_timer_frequency);
    SELFTEST_RUN(test_hal_timer_ns);
    SELFTEST_RUN(test_hal_irq_register_rejects);
    SELFTEST_RUN(test_pic_boot_mask);
    SELFTEST_RUN(test_ps2_mouse_irq);
    SELFTEST_RUN(test_hal_irq_unmask_rejects);
    SELFTEST_RUN(test_hal_irq_unmask_master);
    SELFTEST_RUN(test_hal_irq_unmask_slave);
    SELFTEST_RUN(test_pic_spurious);
    SELFTEST_RUN(test_pic_unmask_keeps_interrupt_flag);
    SELFTEST_RUN(test_multiboot_data_mapped);
}

void selftest_platform_exit(int passed)
{
    outb(QEMU_EXIT_PORT, passed ? QEMU_EXIT_PASS : QEMU_EXIT_FAIL);

    /* Not running under QEMU with isa-debug-exit: stop here. */
    hal_cpu_halt();
}

#endif
