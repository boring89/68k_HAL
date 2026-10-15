/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/irq.h"

#include "pic.h"

#include "arch/x86/interrupt/irq_stub.h"

#include <stddef.h>
#include <stdint.h>

/* Zeroed with the rest of .bss when the kernel is loaded. */
static hal_irq_handler_t handlers[PIC_LINE_COUNT];

int hal_irq_register(unsigned int line, hal_irq_handler_t handler)
{
    if (line >= PIC_LINE_COUNT)
        return -1;

    if (handler == NULL)
        return -1;

    if (handlers[line] != NULL)
        return -1;

    handlers[line] = handler;

    return 0;
}

int hal_irq_unmask(unsigned int line)
{
    if (line >= PIC_LINE_COUNT)
        return -1;

    pic_unmask(line);

    return 0;
}

/* Called by the x86 IRQ stubs (irq_stub.asm). */
void irq_handler(uint32_t irq)
{
    if (pic_handle_spurious(irq))
        return;

    if (irq < PIC_LINE_COUNT && handlers[irq] != NULL)
        handlers[irq]();

    pic_send_eoi((uint8_t)irq);
}
