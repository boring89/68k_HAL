/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "bga.h"

#include "arch/x86/io/io.h"

/*
 * DISPI registers: write the register number to the index port, then read
 * or write the value at the data port (16-bit accesses).
 */
#define DISPI_INDEX_PORT 0x01CE
#define DISPI_DATA_PORT 0x01CF

#define DISPI_ID 0x0
#define DISPI_XRES 0x1
#define DISPI_YRES 0x2
#define DISPI_BPP 0x3
#define DISPI_ENABLE 0x4
#define DISPI_VIRT_WIDTH 0x6

#define DISPI_ID_MIN 0xB0C0 /* version ID values 0xB0C0 .. 0xB0C5 */
#define DISPI_ID_MAX 0xB0CF

#define DISPI_DISABLED 0x00
#define DISPI_ENABLED 0x01
#define DISPI_GETCAPS 0x02 /* while set, XRES/YRES/BPP read as the maximum */
#define DISPI_LFB_ENABLED 0x40

#define MIN_WIDTH 640
#define MIN_HEIGHT 480

static int present;
static uint32_t framebuffer_address;
static uint32_t max_width;
static uint32_t max_height;

static uint16_t read_register(uint16_t index)
{
    outw(DISPI_INDEX_PORT, index);

    return inw(DISPI_DATA_PORT);
}

static void write_register(uint16_t index, uint16_t value)
{
    outw(DISPI_INDEX_PORT, index);
    outw(DISPI_DATA_PORT, value);
}

static int bga_probe(const hal_pci_device_t *pci)
{
    if (present || pci->bar[0].type != HAL_PCI_BAR_MEM || pci->bar[0].base >= 0x100000000ull)
        return -1;

    uint16_t id = read_register(DISPI_ID);

    if (id < DISPI_ID_MIN || id > DISPI_ID_MAX)
        return -1;

    hal_pci_enable(pci, HAL_PCI_COMMAND_IO | HAL_PCI_COMMAND_MEMORY);
    framebuffer_address = (uint32_t)pci->bar[0].base;

    /*
     * Ask for the limits: with GETCAPS added to the enable register, XRES and YRES read
     * as the maximum. The other bits must stay as they were (a mode GRUB set up is
     * enabled; writing GETCAPS alone would switch the screen off).
     */
    uint16_t enable = read_register(DISPI_ENABLE);

    write_register(DISPI_ENABLE, enable | DISPI_GETCAPS);
    max_width = read_register(DISPI_XRES);
    max_height = read_register(DISPI_YRES);
    write_register(DISPI_ENABLE, enable);

    present = 1;

    return 0;
}

int bga_available(void)
{
    return present;
}

void bga_limits(uint32_t *width, uint32_t *height)
{
    *width = present ? max_width : 0;
    *height = present ? max_height : 0;
}

int bga_set_mode(uint32_t width, uint32_t height, uint32_t *framebuffer)
{
    if (!present || width < MIN_WIDTH || height < MIN_HEIGHT || width > max_width ||
        height > max_height)
        return -1;

    /* Switch the adapter off, change the geometry, switch it on with the linear frame buffer. */
    write_register(DISPI_ENABLE, DISPI_DISABLED);
    write_register(DISPI_XRES, (uint16_t)width);
    write_register(DISPI_YRES, (uint16_t)height);
    write_register(DISPI_BPP, 32);
    write_register(DISPI_VIRT_WIDTH, (uint16_t)width);
    write_register(DISPI_ENABLE, DISPI_ENABLED | DISPI_LFB_ENABLED);

    if (read_register(DISPI_XRES) != width || read_register(DISPI_YRES) != height)
        return -1;

    *framebuffer = framebuffer_address;

    return 0;
}

const hal_pci_driver_t bga_pci_driver = {
    .name = "bga",
    .vendor_id = 0x1234,
    .device_id = 0x1111,
    .probe = bga_probe,
};
