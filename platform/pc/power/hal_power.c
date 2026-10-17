/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/power.h"

#include "hal/acpi.h"
#include "hal/cpu.h"
#include "hal/mmu.h"
#include "hal/timer.h"

#include "platform/pc/vmap/vmap.h"

#include "arch/x86/io/io.h"

#include "kernel/printf/printf.h"

#include <stddef.h>

/*
 * ACPI soft off (ACPI Specification 6.x 4.8.3.7 and 16.1): write SLP_TYPx (the numbers from the DSDT's \_S5
 * object) in bits 12:10 of PM1a_CNT (and PM1b_CNT) together with SLP_EN, bit 13. The machine must be in ACPI
 * mode first: PM1a_CNT bit 0 (SCI_EN) set; if it is not, the firmware is asked to switch with the
 * ACPI_ENABLE value written to the SMI command port.
 */
#define PM1_SCI_EN 0x0001u
#define PM1_SLP_EN (1u << 13)

static void wait_ms(unsigned int ms)
{
    uint64_t end = hal_timer_ns() + (uint64_t)ms * 1000000u;

    while (hal_timer_ns() < end)
        ;
}

void hal_power_off(void)
{
    const hal_acpi_info_t *acpi = hal_acpi_info();

    hal_cpu_disable_interrupts();

    if (acpi != NULL && acpi->s5_valid && acpi->pm1a_control_port != 0)
    {
        if (!(inw((uint16_t)acpi->pm1a_control_port) & PM1_SCI_EN) && acpi->smi_command_port != 0 &&
            acpi->acpi_enable_value != 0)
        {
            outb((uint16_t)acpi->smi_command_port, acpi->acpi_enable_value);

            for (int i = 0; i < 300 && !(inw((uint16_t)acpi->pm1a_control_port) & PM1_SCI_EN); i++)
                wait_ms(10);
        }

        outw((uint16_t)acpi->pm1a_control_port, (uint16_t)((acpi->s5_type_a << 10) | PM1_SLP_EN));

        if (acpi->pm1b_control_port != 0)
            outw((uint16_t)acpi->pm1b_control_port, (uint16_t)((acpi->s5_type_b << 10) | PM1_SLP_EN));

        wait_ms(1000);
    }

    /* Virtual machines' own ways (QEMU/Bochs: port 0x604 or 0xB004, VirtualBox: 0x4004), harmless on real hardware. */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    kernel_printf("It is safe to turn off the computer.\n");
    hal_cpu_halt();
}

void hal_power_reboot(void)
{
    const hal_acpi_info_t *acpi = hal_acpi_info();

    hal_cpu_disable_interrupts();

    /* The FADT's reset register (an I/O port, or a byte in memory). */
    if (acpi != NULL && acpi->reset_valid)
    {
        if (acpi->reset_space == 1)
        {
            outb((uint16_t)acpi->reset_address, acpi->reset_value);
        }
        else
        {
            volatile uint8_t *reset = vmap(acpi->reset_address, 1, HAL_MMU_WRITABLE | HAL_MMU_UNCACHED);

            if (reset != NULL)
                *reset = acpi->reset_value;
        }

        wait_ms(100);
    }

    /* The keyboard controller's reset line (command 0xFE pulses it), once it is ready for a command. */
    for (int i = 0; i < 100000 && (inb(0x64) & 0x02); i++)
        ;

    outb(0x64, 0xFE);
    wait_ms(100);

    /* The chipset's reset control register: bit 2 = reset, bit 1 = hard reset (OSDev Wiki "Reboot"). */
    outb(0xCF9, 0x02);
    outb(0xCF9, 0x0E);
    wait_ms(100);

    /* Last resort: a triple fault (an empty IDT, then an interrupt). */
    struct
    {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) empty = {0, 0};

    asm volatile("lidt %0; int3" : : "m"(empty));

    hal_cpu_halt();
}
