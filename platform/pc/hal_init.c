/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/init.h"

#include "platform/pc/interrupt/pic.h"
#include "platform/pc/timer/pit.h"

#include "arch/x86/interrupt/idt.h"
#include "arch/x86/mmu/paging.h"

#include "kernel/log/log.h"

#define PC_TIMER_FREQUENCY 100 /* Hz */

void hal_init(void)
{
    idt_init();
    kernel_log(LOG_INFO, "IDT loaded");

    /* After idt_init so a page fault is reported instead of triple faulting. */
    paging_init();
    kernel_log(LOG_INFO, "Paging enabled");

    pic_init();
    kernel_log(LOG_INFO, "PIC initialized");

    /* After pic_init: the PIT unmasks its line. */
    pit_init(PC_TIMER_FREQUENCY);
}
