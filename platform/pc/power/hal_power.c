/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/power.h"

#include "hal/acpi.h"
#include "hal/cpu.h"
#include "hal/irq.h"
#include "hal/mmu.h"
#include "hal/timer.h"

#include "platform/pc/vmap/vmap.h"

#include "arch/x86/cpu/cpu.h"
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

/* Switch the machine to ACPI mode (SCI_EN set) if the firmware still has it in legacy mode. */
static void enter_acpi_mode(const hal_acpi_info_t *acpi)
{
    if (!(inw((uint16_t)acpi->pm1a_control_port) & PM1_SCI_EN) && acpi->smi_command_port != 0 &&
        acpi->acpi_enable_value != 0)
    {
        outb((uint16_t)acpi->smi_command_port, acpi->acpi_enable_value);

        for (int i = 0; i < 300 && !(inw((uint16_t)acpi->pm1a_control_port) & PM1_SCI_EN); i++)
            wait_ms(10);
    }
}

void hal_power_off(void)
{
    const hal_acpi_info_t *acpi = hal_acpi_info();

    hal_cpu_disable_interrupts();

    if (acpi != NULL && acpi->s5_valid && acpi->pm1a_control_port != 0)
    {
        enter_acpi_mode(acpi);

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

/*
 * The power button as an ACPI fixed event (ACPI Specification 6.x 4.8.3.1, tables 4.13 and 4.14): PWRBTN_STS,
 * bit 8 of the PM1 status register (the first half of PM1a_EVT_BLK), is set when the button is pressed and
 * cleared by writing 1 to it; PWRBTN_EN, bit 8 of the enable register (the second half), makes it raise the
 * SCI (FADT SCI_INT, normally ISA line 9). The SCI handler clears the bit and remembers the press. QEMU only
 * sets the status bit while the enable bit is on, so it must be on; on hardware the status is set anyway,
 * so polling still works if the SCI line cannot be had. In legacy mode the firmware handles the button
 * itself, so ACPI mode is entered first.
 *
 * Any other source of the SCI (a general-purpose event the firmware left enabled) cannot be handled here
 * (no AML): after SCI_UNKNOWN_LIMIT of them in a row the line is masked again and the button is polled.
 */
#define PM1_PWRBTN (1u << 8)
#define SCI_UNKNOWN_LIMIT 64

static volatile int button_pending;
static volatile int sci_unknown;
static int sci_line = -1;

/* The PM1 event registers of block a (b = 0) or b (b = 1); 0 if absent. */
static uint16_t pm1_status_port(const hal_acpi_info_t *acpi, int b)
{
    return (uint16_t)(b ? acpi->pm1b_event_port : acpi->pm1a_event_port);
}

static uint16_t pm1_enable_port(const hal_acpi_info_t *acpi, int b)
{
    uint16_t base = pm1_status_port(acpi, b);
    uint8_t half = acpi->pm1_event_length >= 4 ? acpi->pm1_event_length / 2 : 2;

    return base != 0 ? (uint16_t)(base + half) : 0;
}

/* Clear PWRBTN_STS in both blocks; 1 if it was set in either. */
static int take_button_status(const hal_acpi_info_t *acpi)
{
    int pressed = 0;

    for (int b = 0; b < 2; b++)
    {
        uint16_t port = pm1_status_port(acpi, b);

        if (port != 0 && (inw(port) & PM1_PWRBTN))
        {
            outw(port, PM1_PWRBTN);
            pressed = 1;
        }
    }

    return pressed;
}

static void sci_handler(void)
{
    const hal_acpi_info_t *acpi = hal_acpi_info();

    if (take_button_status(acpi))
    {
        button_pending = 1;
        sci_unknown = 0;
        return;
    }

    if (++sci_unknown >= SCI_UNKNOWN_LIMIT && sci_line >= 0)
    {
        hal_irq_mask((unsigned int)sci_line);
        sci_line = -1;
    }
}

int hal_power_button_pressed(void)
{
    static int ready;
    const hal_acpi_info_t *acpi = hal_acpi_info();

    if (acpi == NULL || acpi->pm1a_event_port == 0 || acpi->pm1a_control_port == 0)
        return 0;

    if (!ready)
    {
        ready = 1;
        enter_acpi_mode(acpi);

        /* A press from before (e.g. the one that turned the machine on) does not count. */
        take_button_status(acpi);

        for (int b = 0; b < 2; b++)
        {
            uint16_t port = pm1_enable_port(acpi, b);

            if (port != 0)
                outw(port, (uint16_t)(inw(port) | PM1_PWRBTN));
        }

        if (acpi->sci_irq < 16 && hal_irq_register(acpi->sci_irq, sci_handler) == 0 &&
            hal_irq_unmask(acpi->sci_irq) == 0)
            sci_line = acpi->sci_irq;

        return 0;
    }

    /* Polling as well: the SCI line may be masked or shared. */
    uint32_t flags = cpu_irq_save();
    int pressed = button_pending || take_button_status(acpi);

    button_pending = 0;
    cpu_irq_restore(flags);

    return pressed;
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
        uintptr_t base; /* 4 bytes in protected mode, 8 in long mode */
    } __attribute__((packed)) empty = {0, 0};

    asm volatile("lidt %0; int3" : : "m"(empty));

    hal_cpu_halt();
}
