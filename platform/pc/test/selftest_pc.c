/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* PC platform self tests, run by selftest_run (-DKERNEL_SELFTEST builds only). */
#ifdef KERNEL_SELFTEST

#include "kernel/lib/k_string.h"
#include "platform/pc/interrupt/lapic.h"

#include "kernel/test/selftest.h"

#include "hal/acpi.h"
#include "hal/boot.h"
#include "hal/cpu.h"
#include "hal/dma.h"
#include "hal/irq.h"
#include "hal/mmu.h"
#include "hal/pci.h"
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

/* QEMU (SeaBIOS) provides ACPI tables with at least one processor and a local APIC. */
static void test_acpi_tables(void)
{
    const hal_acpi_info_t *info = hal_acpi_info();

    SELFTEST_ASSERT(info != NULL);

    if (info == NULL)
        return;

    SELFTEST_ASSERT(info->cpu_count >= 1);
    SELFTEST_ASSERT(info->lapic_base == 0xFEE00000u);
    SELFTEST_ASSERT(info->ioapic_count >= 1);
}

/* QEMU's default machine (i440FX): host bridge, PIIX3 (ISA, IDE, ACPI) and the standard VGA. */
static void test_pci_devices(void)
{
    SELFTEST_ASSERT(hal_pci_device_count() >= 4);

    int host = hal_pci_find(0x8086, 0x1237, 0);
    int isa = hal_pci_find_class(0x06, 0x01, 0);
    int ide = hal_pci_find(0x8086, 0x7010, 0);
    int vga = hal_pci_find(0x1234, 0x1111, 0);

    SELFTEST_ASSERT(host == 0);
    SELFTEST_ASSERT(isa >= 0);
    SELFTEST_ASSERT(ide >= 0);
    SELFTEST_ASSERT(vga >= 0);
    SELFTEST_ASSERT(hal_pci_find(0xDEAD, 0xBEEF, 0) == -1);

    hal_pci_device_t dev;

    if (vga < 0 || hal_pci_device((unsigned int)vga, &dev) != 0)
        return;

    SELFTEST_ASSERT(dev.class_code == 0x03 && dev.subclass == 0x00);

    /* BAR 0 of the standard VGA is its 16 MiB frame buffer, BAR 2 the 4 KiB register block. */
    SELFTEST_ASSERT(dev.bar[0].type == HAL_PCI_BAR_MEM);
    SELFTEST_ASSERT(dev.bar[0].size == 16u * 1024 * 1024);
    SELFTEST_ASSERT(dev.bar[0].prefetchable);
    SELFTEST_ASSERT(dev.bar[2].type == HAL_PCI_BAR_MEM);
    SELFTEST_ASSERT(dev.bar[2].size == 4096);

    /* Configuration space reads agree with what was enumerated. */
    SELFTEST_ASSERT(hal_pci_config_read(&dev, 0, 2) == 0x1234);
    SELFTEST_ASSERT(hal_pci_config_read(&dev, 2, 2) == 0x1111);
    SELFTEST_ASSERT(hal_pci_config_read(&dev, 0, 4) == 0x11111234u);
    SELFTEST_ASSERT(hal_pci_config_read(&dev, 0x0B, 1) == 0x03);

    /* A write to the command register sticks (bus mastering on) and the other bits stay. */
    uint32_t before = hal_pci_config_read(&dev, 4, 2);

    hal_pci_enable(&dev, HAL_PCI_COMMAND_BUS_MASTER);
    SELFTEST_ASSERT((hal_pci_config_read(&dev, 4, 2) & 0x4) != 0);
    SELFTEST_ASSERT((hal_pci_config_read(&dev, 4, 2) & before) == before);

    /* The register block can be mapped and read: the VGA ID port reads back a BGA version. */
    volatile uint16_t *registers = hal_pci_map_bar(&dev, 2);

    SELFTEST_ASSERT(registers != NULL);

    if (registers != NULL)
    {
        /* Bochs DISPI registers sit at offset 0x500 (index 0 = ID, 0xB0C0 .. 0xB0CF). */
        uint16_t id = registers[0x500 / 2];

        SELFTEST_ASSERT(id >= 0xB0C0 && id <= 0xB0CF);
    }

    SELFTEST_ASSERT(hal_pci_map_bar(&dev, 1) == NULL); /* not a memory BAR */
}

/* test-qemu starts QEMU with -smp 4: every processor must have reported. */
static void test_smp_processors_online(void)
{
    const hal_acpi_info_t *info = hal_acpi_info();

    SELFTEST_ASSERT(info != NULL && info->cpu_count == 4);
    SELFTEST_ASSERT(hal_cpu_online_count() == 4);
}

/* DMA memory: contiguous, zeroed, with the physical address a device would use. */
static void test_dma_alloc(void)
{
    uint32_t phys = 0;
    volatile uint32_t *memory = hal_dma_alloc(3 * 4096 + 1, &phys);

    SELFTEST_ASSERT(memory != NULL);

    if (memory == NULL)
        return;

    SELFTEST_ASSERT(phys != 0 && (phys & 0xFFF) == 0);

    /* Four pages, zeroed, writable, and contiguous in physical memory. */
    for (uint32_t i = 0; i < 4 * 1024; i++)
        SELFTEST_ASSERT(memory[i] == 0);

    memory[0] = 0x11223344;
    memory[4 * 1024 - 1] = 0x55667788;

    uintptr_t first;
    uintptr_t last;

    SELFTEST_ASSERT(hal_mmu_get_phys((uintptr_t)memory, &first) == 0);
    SELFTEST_ASSERT(hal_mmu_get_phys((uintptr_t)memory + 3 * 4096, &last) == 0);
    SELFTEST_ASSERT(first == phys);
    SELFTEST_ASSERT(last == phys + 3 * 4096);

    hal_dma_free((void *)memory, 3 * 4096 + 1);
    SELFTEST_ASSERT(hal_mmu_get_phys((uintptr_t)memory, &first) == -1);

    SELFTEST_ASSERT(hal_dma_alloc(0, &phys) == NULL);
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

/* The Multiboot data stays readable for the shell's mmap (hal_init identity maps it; a 64-bit kernel's direct map covers it). */
static void test_multiboot_data_mapped(void)
{
    const multiboot_info_t *mbi = multiboot_get_info();
    uintptr_t address = (uintptr_t)mbi;
    uintptr_t phys = 0;

    SELFTEST_ASSERT(hal_mmu_get_phys(address, &phys) == 0);
    SELFTEST_ASSERT(phys == multiboot_info_phys());
    SELFTEST_ASSERT(hal_mmu_get_phys(address + sizeof(*mbi) - 1, &phys) == 0);

    SELFTEST_ASSERT(mbi->flags & MULTIBOOT_INFO_MEM_MAP);

    uintptr_t mmap_virt = (uintptr_t)hal_phys_to_virt(mbi->mmap_addr);
    uintptr_t mmap_end = mmap_virt + mbi->mmap_length;

    SELFTEST_ASSERT(hal_mmu_get_phys(mmap_virt, &phys) == 0);
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
    SELFTEST_ASSERT(hal_irq_register(HAL_IRQ_LINE_COUNT, unused_irq_handler) == -1);
    SELFTEST_ASSERT(hal_irq_register(0xFFFFFFFFu, unused_irq_handler) == -1);
    SELFTEST_ASSERT(hal_irq_register(6, NULL) == -1);

    /* Line 0 already belongs to the PIT. */
    SELFTEST_ASSERT(hal_irq_register(0, unused_irq_handler) == -1);

    /* The PIT handler must still be the one running. */
    uint32_t start = hal_timer_ticks();
    hal_cpu_idle();
    SELFTEST_ASSERT(hal_timer_ticks() != start);
}

/* The tests of the 8259's mask register only make sense while the 8259 delivers the interrupts. */
static int using_8259(void)
{
    return hal_irq_controller()[0] == '8';
}

/* 8259A data ports; reading them returns the IMR (OSDev Wiki "8259 PIC"). */
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_DATA 0xA1

/*
 * pic_init masks everything; the PIT (0), keyboard (1) and mouse (12) unmask,
 * and the mouse is on the slave, so the cascade line (2) is open too. The
 * e1000 card of the test run (QEMU puts it on IRQ 11) unmasks line 11.
 */
#define BOOT_MASTER_MASK 0xF8
#define BOOT_SLAVE_MASK 0xE7

static void test_pic_boot_mask(void)
{
    if (!using_8259())
    {
        /* With the APIC in use the 8259 is silent: every line stays masked. */
        SELFTEST_ASSERT(inb(PIC_MASTER_DATA) == 0xFF);
        SELFTEST_ASSERT(inb(PIC_SLAVE_DATA) == 0xFF);

        return;
    }

    SELFTEST_ASSERT(inb(PIC_MASTER_DATA) == BOOT_MASTER_MASK);
    SELFTEST_ASSERT(inb(PIC_SLAVE_DATA) == BOOT_SLAVE_MASK);
}

/*
 * QEMU's machines have a MADT with an I/O APIC: the interrupts must run through it (the timer
 * ticks already prove it works). The ISA timer line goes to input 2 (the MADT override).
 */
static void test_apic_in_use(void)
{
    const hal_acpi_info_t *info = hal_acpi_info();

    const char *cmdline = hal_boot_cmdline();

    SELFTEST_ASSERT(info != NULL && info->ioapic_count >= 1);

    /* make test-noapic runs this test with "noapic": then the 8259 is the controller. */
    SELFTEST_ASSERT(using_8259() == (cmdline != NULL && k_strstr(cmdline, "noapic") != NULL));
    SELFTEST_ASSERT(info->irq_override_count >= 1);
    SELFTEST_ASSERT(info->irq_override[0].source == 0 && info->irq_override[0].gsi == 2);
}

static volatile int msi_handler_calls;

static void msi_test_handler(void)
{
    msi_handler_calls++;
}

/*
 * An MSI line: allocate one, then raise its vector with a self-IPI (an interrupt the local APIC
 * sends to its own CPU: ICR with destination shorthand "self", Intel SDM Vol.3A 10.6.1). That goes
 * the same way as a device's message: vector -> IDT stub for line 16 + n -> irq_handler -> handler.
 */
static void test_msi_line_delivery(void)
{
    uint32_t address = 0;
    uint32_t data = 0;
    int line = hal_irq_msi_alloc(msi_test_handler, &address, &data);

    if (using_8259())
    {
        SELFTEST_ASSERT(line == -1);

        return;
    }

    SELFTEST_ASSERT(line >= 16 && line < HAL_IRQ_LINE_COUNT);
    SELFTEST_ASSERT((address & 0xFFF00000u) == 0xFEE00000u);
    SELFTEST_ASSERT(data == 0x20u + (uint32_t)line);

    /* A second one gets another line, and its handler cannot be registered again. */
    SELFTEST_ASSERT(hal_irq_register((unsigned int)line, msi_test_handler) == -1);

    msi_handler_calls = 0;
    lapic_write(LAPIC_ICR_LOW, (1u << 18) | (1u << 14) | data); /* self, level assert, fixed, vector */

    for (int i = 0; i < 1000000 && msi_handler_calls == 0; i++)
        asm volatile("pause");

    SELFTEST_ASSERT(msi_handler_calls == 1);
}

/* The capability list walker finds what QEMU's devices have. */
static void test_pci_capabilities(void)
{
    /* The xHCI controller of the test run (prog_if 0x30) has MSI-X; UHCI and EHCI have no capabilities. */
    for (int index = hal_pci_find_class(0x0C, 0x03, 0); index >= 0;
         index = hal_pci_find_class(0x0C, 0x03, (unsigned int)index + 1))
    {
        hal_pci_device_t device;

        hal_pci_device(index, &device);

        if (device.prog_if == 0x30)
        {
            SELFTEST_ASSERT(hal_pci_find_capability(&device, HAL_PCI_CAP_MSIX) != 0);

            /* Not turned on here (the real driver does that): QEMU's xHCI is MSI-X only. */
        }
        else
        {
            SELFTEST_ASSERT(hal_pci_find_capability(&device, HAL_PCI_CAP_MSI) == 0);
            SELFTEST_ASSERT(hal_pci_find_capability(&device, HAL_PCI_CAP_MSIX) == 0);
        }
    }

    hal_pci_device_t bridge;

    SELFTEST_ASSERT(hal_pci_device(0, &bridge) == 0);
    SELFTEST_ASSERT(hal_pci_find_capability(&bridge, 0x7F) == 0);
}

/* The mouse initialised in hal_init and has its handler on IRQ 12. */
static void test_ps2_mouse_irq(void)
{
    if (using_8259())
        SELFTEST_ASSERT((inb(PIC_SLAVE_DATA) & (1u << 4)) == 0);

    SELFTEST_ASSERT(hal_irq_register(12, unused_irq_handler) == -1);
}

static void test_hal_irq_unmask_rejects(void)
{
    SELFTEST_ASSERT(hal_irq_unmask(HAL_IRQ_LINE_COUNT) == -1);
    SELFTEST_ASSERT(hal_irq_unmask(0xFFFFFFFFu) == -1);

    if (!using_8259())
        return;

    SELFTEST_ASSERT(inb(PIC_MASTER_DATA) == BOOT_MASTER_MASK);
    SELFTEST_ASSERT(inb(PIC_SLAVE_DATA) == BOOT_SLAVE_MASK);
}

static void test_hal_irq_unmask_master(void)
{
    if (!using_8259())
        return;

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
    if (!using_8259())
        return;

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
    if (!using_8259())
        return;

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
    if (!using_8259())
        return;

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
    SELFTEST_RUN(test_acpi_tables);
    SELFTEST_RUN(test_pci_devices);
    SELFTEST_RUN(test_dma_alloc);
    SELFTEST_RUN(test_smp_processors_online);
    SELFTEST_RUN(test_hal_irq_register_rejects);
    SELFTEST_RUN(test_apic_in_use);
    SELFTEST_RUN(test_msi_line_delivery);
    SELFTEST_RUN(test_pci_capabilities);
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
