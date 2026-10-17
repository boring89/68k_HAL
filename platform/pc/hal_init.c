/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/init.h"

#include "drivers/ata/ata.h"
#include "drivers/ps2_keyboard/ps2_keyboard.h"
#include "drivers/ps2_mouse/ps2_mouse.h"
#include "drivers/serial/serial.h"
#include "drivers/vga/vga.h"

#include "platform/pc/acpi/acpi.h"
#include "platform/pc/boot/multiboot.h"
#include "platform/pc/interrupt/irq.h"
#include "platform/pc/interrupt/pic.h"
#include "platform/pc/pci/pci.h"
#include "hal/pci.h"
#include "platform/pc/smp/smp.h"
#include "platform/pc/video/framebuffer.h"
#include "platform/pc/timer/pit.h"
#include "platform/pc/timer/tsc.h"

#include "arch/x86/interrupt/idt.h"
#include "arch/x86/mmu/paging.h"

#include "kernel/log/log.h"
#include "kernel/main.h"
#include "kernel/panic/panic.h"
#include "kernel/printf/printf.h"

#include "hal/boot.h"
#include "hal/cpu.h"
#include "hal/mmu.h"

#include <stdbool.h>
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

static bool boot_known(void)
{
    return boot_magic == MULTIBOOT_BOOTLOADER_MAGIC ||
           boot_magic == MULTIBOOT2_BOOTLOADER_MAGIC;
}

/* Parse the boot loader's information (Multiboot 1 or 2). */
static void boot_parse(void)
{
    if (boot_magic == MULTIBOOT_BOOTLOADER_MAGIC)
        multiboot_init(boot_info);
    else if (boot_magic == MULTIBOOT2_BOOTLOADER_MAGIC)
        multiboot2_init(boot_info);
}

void hal_init_console(void)
{
    /*
     * In a graphics mode there is no VGA text buffer, so the console only
     * goes to serial. Multiboot is parsed here already (and again, with
     * logging, in hal_init_early) to find out which mode we are in.
     */
    boot_parse();

    if (!boot_known() || !pc_framebuffer_detect())
        text_display_register(&vga_text_display);

    serial_init();
    console_register_sink(&serial_console_sink);
}

void hal_init_early(void)
{
    if (!boot_known())
    {
        kernel_log(LOG_ERROR, "Invalid Multiboot magic");

        hal_cpu_halt();
    }

    boot_parse();

    kernel_log(LOG_INFO, "Multiboot initialized");
    kernel_printf("Boot protocol: Multiboot%d\n",
                  boot_magic == MULTIBOOT2_BOOTLOADER_MAGIC ? 2 : 1);

    kernel_printf("Multiboot flags: %x\n", multiboot_get_info()->flags);

    /* What the loader says about the screen: useful when the console stays dark on a new machine. */
    const multiboot_info_t *mbi = multiboot_get_info();

    if (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER)
    {
        kernel_printf("Loader framebuffer: %x %ux%u pitch %u %u bpp type %u%s\n",
                      (unsigned int)mbi->framebuffer_addr, mbi->framebuffer_width, mbi->framebuffer_height,
                      mbi->framebuffer_pitch, mbi->framebuffer_bpp, mbi->framebuffer_type,
                      multiboot_framebuffer_usable() ? "" : " (not usable)");

        if (multiboot_colors_known())
        {
            kernel_printf("  colors (position/size): red %u/%u green %u/%u blue %u/%u\n", mbi->color_info[0],
                          mbi->color_info[1], mbi->color_info[2], mbi->color_info[3], mbi->color_info[4],
                          mbi->color_info[5]);
        }
    }
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
    hal_cpu_init();

    idt_init();
    kernel_log(LOG_INFO, "IDT loaded");

    /* After idt_init so a page fault is reported instead of triple faulting. */
    paging_init();
    map_boot_data();
    kernel_log(LOG_INFO, "Paging enabled");

    if (pc_framebuffer_detect())
    {
        if (pc_framebuffer_map() == 0)
            kernel_log(LOG_INFO, "Framebuffer mapped");
        else
            kernel_log(LOG_ERROR, "Cannot map the framebuffer");
    }

    if (acpi_init() == 0)
        kernel_log(LOG_INFO, "ACPI tables found");
    else
        kernel_log(LOG_WARN, "No ACPI tables");

    kernel_printf("PCI devices: %u\n", pci_init());

    pic_init();
    kernel_log(LOG_INFO, "PIC initialized");

    /* Before the timer and the keyboard unmask their lines: it decides where those lines go. */
    if (irq_apic_enable() == 0)
        kernel_log(LOG_INFO, "APIC interrupts enabled");

    /* After pic_init: the PIT, the keyboard and the mouse unmask their lines. */
    pit_init(PC_TIMER_FREQUENCY);
    ps2_keyboard_init();

    if (tsc_init() == 0)
        kernel_printf("TSC: %u kHz\n", tsc_khz());
    else
        kernel_log(LOG_WARN, "No usable TSC");

    kernel_printf("CPUs online: %u\n", smp_start());

    if (ps2_mouse_init() == 0)
        kernel_log(LOG_INFO, "PS/2 mouse initialized");
    else
        kernel_log(LOG_WARN, "No PS/2 mouse");

    /* Registers the disk as a block device; nothing else needs it yet. */
    if (ata_init() == 0)
        kernel_log(LOG_INFO, "ATA disk found");

    /* Last: drivers for PCI cards (network...) use the timer and the interrupt controller. */
    if (hal_pci_probe_drivers() > 0)
        kernel_log(LOG_INFO, "PCI drivers started");
}
