/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/init.h"

#include "drivers/serial/serial.h"

#include "platform/pc/boot/multiboot.h"
#include "platform/pc/interrupt/pic.h"
#include "platform/pc/timer/pit.h"

#include "arch/x86/interrupt/idt.h"
#include "arch/x86/mmu/paging.h"

#include "kernel/log/log.h"
#include "kernel/main.h"
#include "kernel/panic/panic.h"
#include "kernel/printf/printf.h"

#include "hal/boot.h"
#include "hal/cpu.h"
#include "hal/mmu.h"

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
    serial_init();
    console_register_sink(&serial_console_sink);

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

/*
 * Identity map every page overlapping [start, end) that is not mapped
 * yet, read-only. Returns -1 if that would map page 0 (the null pointer
 * guard) or hal_mmu_map fails.
 */
static int identity_map_range(uint64_t start, uint64_t end)
{
    uint64_t page = start & ~(uint64_t)(HAL_MMU_PAGE_SIZE - 1);

    for (; page < end; page += HAL_MMU_PAGE_SIZE)
    {
        uintptr_t phys;

        if (hal_mmu_get_phys((uintptr_t)page, &phys) == 0)
            continue;

        if (page == 0 || hal_mmu_map((uintptr_t)page, (uintptr_t)page, 0) != 0)
            return -1;
    }

    return 0;
}

/*
 * The shell reads the boot memory map long after boot. The boot loader
 * decides where the Multiboot data lives, so it may be outside the
 * first 4 MiB (QEMU -kernel puts it at 0x9500, GRUB at 0x10000).
 */
static void map_boot_data(void)
{
    hal_memory_region_t region;

    for (unsigned int i = 0; hal_boot_reserved_region(i, &region) == 0; i++)
    {
        if (identity_map_range(region.base, region.base + region.length) != 0)
            panic("boot: cannot map boot data at %x", (unsigned int)region.base);
    }
}

void hal_init(void)
{
    idt_init();
    kernel_log(LOG_INFO, "IDT loaded");

    /* After idt_init so a page fault is reported instead of triple faulting. */
    paging_init();
    map_boot_data();
    kernel_log(LOG_INFO, "Paging enabled");

    pic_init();
    kernel_log(LOG_INFO, "PIC initialized");

    /* After pic_init: the PIT unmasks its line. */
    pit_init(PC_TIMER_FREQUENCY);
}
