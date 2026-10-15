/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/init.h"

#include "platform/pc/boot/multiboot.h"
#include "platform/pc/interrupt/pic.h"
#include "platform/pc/timer/pit.h"

#include "arch/x86/interrupt/idt.h"
#include "arch/x86/mmu/paging.h"

#include "kernel/log/log.h"
#include "kernel/main.h"
#include "kernel/printf/printf.h"

#include "hal/cpu.h"

#include <stdint.h>

#define PC_TIMER_FREQUENCY 100 /* Hz */

/* EAX and EBX from the boot loader, saved by pc_entry. */
static uint32_t boot_magic;
static uint32_t boot_info;

/*
 * Called by _start (boot.asm) with the Multiboot magic and the address
 * of the Multiboot info structure. Nothing is set up yet, not even the
 * console, so only save them for hal_init_early.
 */
void pc_entry(uint32_t magic, uint32_t info)
{
    boot_magic = magic;
    boot_info = info;

    kernel_main();
}

void hal_init_early(void)
{
    if (boot_magic != MULTIBOOT_BOOTLOADER_MAGIC)
    {
        kernel_log(LOG_ERROR, "Invalid Multiboot magic");

        hal_cpu_halt();
    }

    multiboot_init(boot_info);

    kernel_log(LOG_INFO, "Multiboot initialized");

    kernel_printf("Multiboot flags: %x\n", multiboot_get_info()->flags);
    kernel_printf("Multiboot info: %x\n", boot_info);
}

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
