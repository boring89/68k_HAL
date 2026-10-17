/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/irq.h"

#include "pic.h"
#include "irq.h"
#include "ioapic.h"
#include "lapic.h"

#include "hal/acpi.h"
#include "hal/boot.h"
#include "hal/cpu.h"

#include "arch/x86/interrupt/irq_stub.h"

#include "kernel/lib/k_string.h"
#include "kernel/printf/printf.h"

#include <stddef.h>
#include <stdint.h>

/* Zeroed with the rest of .bss when the kernel is loaded. */
static hal_irq_handler_t handlers[HAL_IRQ_LINE_COUNT];

/* 1 once the local APIC and I/O APIC have taken over from the 8259. */
static int apic_mode;

static volatile unsigned int irq_total;

unsigned int hal_irq_count(void)
{
    return irq_total;
}

int hal_irq_register(unsigned int line, hal_irq_handler_t handler)
{
    if (line >= HAL_IRQ_LINE_COUNT)
        return -1;

    if (handler == NULL)
        return -1;

    if (handlers[line] != NULL)
        return -1;

    handlers[line] = handler;

    return 0;
}

/* Where ISA line `line` ends up on the I/O APIC, and with which polarity and trigger mode (ACPI MADT overrides). */
static uint32_t line_to_gsi(unsigned int line, uint32_t *flags)
{
    const hal_acpi_info_t *acpi = hal_acpi_info();

    *flags = 0;

    if (acpi != NULL)
    {
        for (uint32_t i = 0; i < acpi->irq_override_count; i++)
        {
            if (acpi->irq_override[i].source != line)
                continue;

            uint16_t bits = acpi->irq_override[i].flags;

            if ((bits & 3) == 3)
                *flags |= IOAPIC_POLARITY_LOW;

            if (((bits >> 2) & 3) == 3)
                *flags |= IOAPIC_TRIGGER_LEVEL;

            return acpi->irq_override[i].gsi;
        }
    }

    return line;
}

int hal_irq_unmask(unsigned int line)
{
    if (line >= HAL_IRQ_LINE_COUNT)
        return -1;

    /* Lines above 15 are MSI targets: nothing to unmask. */
    if (line >= PIC_LINE_COUNT)
        return 0;

    if (!apic_mode)
    {
        pic_unmask(line);

        return 0;
    }

    uint32_t flags;
    uint32_t gsi = line_to_gsi(line, &flags);

    if (ioapic_route(gsi, (uint8_t)(IRQ_STUB_VECTOR_BASE + line), lapic_id(), flags) != 0)
        return -1;

    return ioapic_unmask(gsi);
}

int irq_apic_enable(void)
{
    const hal_acpi_info_t *acpi = hal_acpi_info();
    const char *cmdline = hal_boot_cmdline();

    if (acpi == NULL || acpi->ioapic_count == 0 || !(hal_cpu_info()->features & HAL_CPU_FEATURE_APIC))
        return -1;

    if (cmdline != NULL && k_strstr(cmdline, "noapic") != NULL)
    {
        kernel_printf("Interrupts: noapic on the command line, using the 8259\n");

        return -1;
    }

    if (lapic_init(acpi->lapic_base) != 0 || ioapic_init() == 0)
        return -1;

    /* pic_init masked every line already; with the APIC in use the 8259 stays silent. */
    apic_mode = 1;

    kernel_printf("Interrupts: I/O APIC (%u inputs), local APIC %u\n", ioapic_input_count(), lapic_id());

    return 0;
}

const char *hal_irq_controller(void)
{
    return apic_mode ? "IOAPIC" : "8259";
}

int hal_irq_msi_alloc(hal_irq_handler_t handler, uint32_t *address, uint32_t *data)
{
    if (!apic_mode || handler == NULL)
        return -1;

    for (unsigned int line = PIC_LINE_COUNT; line < HAL_IRQ_LINE_COUNT; line++)
    {
        if (handlers[line] != NULL)
            continue;

        handlers[line] = handler;

        /*
         * Message address and data for x86 (SDM Vol.3A 10.11): 0xFEE in the top 12 bits, the
         * destination APIC ID in bits 19:12 (physical mode, no redirection); data = vector
         * (fixed delivery, edge triggered).
         */
        *address = 0xFEE00000u | (lapic_id() << 12);
        *data = IRQ_STUB_VECTOR_BASE + line;

        return (int)line;
    }

    return -1;
}

/* Called by the x86 IRQ stubs (irq_stub.asm). */
void irq_handler(uint32_t irq)
{
    irq_total++;

    if (apic_mode)
    {
        if (irq < HAL_IRQ_LINE_COUNT && handlers[irq] != NULL)
            handlers[irq]();

        lapic_eoi();

        return;
    }

    if (pic_handle_spurious(irq))
        return;

    if (irq < PIC_LINE_COUNT && handlers[irq] != NULL)
        handlers[irq]();

    pic_send_eoi((uint8_t)irq);
}
