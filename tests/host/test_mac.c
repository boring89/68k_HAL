/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "emu/m68k/vm.h"
#include "emu/mac/mac.h"

#include <stdint.h>
#include <string.h>

#define ROM_SIZE 0x10000
#define RAM_SIZE 0x100000
#define ROM_ENTRY 0x20

static uint8_t rom[ROM_SIZE];
static uint8_t ram[RAM_SIZE];
static mac_t mac;

static void put16(uint32_t off, uint16_t v)
{
    rom[off] = v >> 8;
    rom[off + 1] = (uint8_t)v;
}

static void put32(uint32_t off, uint32_t v)
{
    put16(off, v >> 16);
    put16(off + 2, (uint16_t)v);
}

/* Reset vectors as the real ROM has them: SSP at 0, PC (in ROM space) at 4. */
static void build_rom(void)
{
    uint32_t p = ROM_ENTRY;

    memset(rom, 0, sizeof rom);
    put32(0, 0x1000);
    put32(4, MAC_ROM_BASE + ROM_ENTRY);

    put16(p, 0x13FC); put16(p + 2, 0x0010); put32(p + 4, 0x50F00600); p += 8;
    /* move.b #$10,$50F00600   VIA1 DDRA: bit 4 (overlay) becomes an output */
    put16(p, 0x13FC); put16(p + 2, 0x0000); put32(p + 4, 0x50F00200); p += 8;
    /* move.b #0,$50F00200     VIA1 ORA: drive it low, overlay off */
    put16(p, 0x23FC); put32(p + 2, 0x12345678); put32(p + 6, 0x100); p += 10;
    /* move.l #$12345678,$100.l */
    put16(p, 0x2038); put16(p + 2, 0x0100); p += 4;
    /* move.l $100.w,D0 */
    put16(p, 0x4E72); put16(p + 2, 0x2700);
    /* stop #$2700 */
}

static void test_init_rejects(void)
{
    TEST_ASSERT_EQ_INT(-1, mac_init(&mac, ram, RAM_SIZE, rom, 12345));
    TEST_ASSERT_EQ_INT(-1, mac_init(&mac, ram, RAM_SIZE, rom, 0x100));
    TEST_ASSERT_EQ_INT(-1, mac_init(&mac, NULL, RAM_SIZE, rom, ROM_SIZE));
    TEST_ASSERT_EQ_INT(0, mac_init(&mac, ram, RAM_SIZE, rom, ROM_SIZE));
}

static void test_overlay_and_rom_mirror(void)
{
    build_rom();
    memset(ram, 0, sizeof ram);
    TEST_ASSERT_EQ_INT(0, mac_init(&mac, ram, RAM_SIZE, rom, ROM_SIZE));
    mac_reset(&mac);

    /* After reset the ROM shows through at 0, and in its own space. */
    TEST_ASSERT(mac.overlay);
    TEST_ASSERT_EQ_INT(0x1000, mac_bus_read(&mac, 0, 4));
    TEST_ASSERT_EQ_INT(MAC_ROM_BASE + ROM_ENTRY, mac_bus_read(&mac, 4, 4));
    TEST_ASSERT_EQ_INT(0x1000, mac_bus_read(&mac, MAC_ROM_BASE, 4));
    TEST_ASSERT_EQ_INT(0x1000,
                       mac_bus_read(&mac, MAC_ROM_BASE + ROM_SIZE, 4));

    /* ROM is read-only; with the overlay on, writes to low memory vanish. */
    mac_bus_write(&mac, 0x100, 4, 0xDEADBEEF);
    mac_bus_write(&mac, MAC_ROM_BASE, 4, 0xDEADBEEF);
    TEST_ASSERT_EQ_INT(0x1000, mac_bus_read(&mac, MAC_ROM_BASE, 4));
    TEST_ASSERT_EQ_INT(0, ram[0x100]);
}

static void test_run_clears_overlay(void)
{
    build_rom();
    memset(ram, 0, sizeof ram);
    TEST_ASSERT_EQ_INT(0, mac_init(&mac, ram, RAM_SIZE, rom, ROM_SIZE));
    mac_reset(&mac);

    mac_run(&mac, 10000);
    TEST_ASSERT(m68k_vm_halted());
    TEST_ASSERT(!mac.overlay);
    TEST_ASSERT_EQ_INT(0x12345678, m68k_vm_get_reg(M68K_VM_REG_D0));
    TEST_ASSERT_EQ_INT(0x12, ram[0x100]);
    TEST_ASSERT_EQ_INT(0x12345678, mac_bus_read(&mac, 0x100, 4));
    TEST_ASSERT_EQ_INT(0, mac_unimpl_total(&mac));
    TEST_ASSERT_EQ_INT(5, mac.insn_count);
}

static void test_unimplemented_io_is_logged(void)
{
    TEST_ASSERT_EQ_INT(0, mac_init(&mac, ram, RAM_SIZE, rom, ROM_SIZE));
    mac_reset(&mac);

    TEST_ASSERT_EQ_INT(0, mac_bus_read(&mac, 0x50F12340, 1));
    mac_bus_write(&mac, 0x50F12340, 2, 0xABCD);
    TEST_ASSERT_EQ_INT(0, mac_bus_read(&mac, 0xA0000000u, 4));
    TEST_ASSERT_EQ_INT(4, mac_unimpl_total(&mac));
    TEST_ASSERT_EQ_INT(0x50F12340, mac.unimpl[0].addr);
    TEST_ASSERT_EQ_INT(4, mac.unimpl_used);
}

static void test_via_timer_and_interrupts(void)
{
    mac_via_t via = { 0 };

    /* Enable T1 interrupt, start a 100-tick one-shot timer. */
    mac_via_write(&via, 14, 0x80 | VIA_INT_T1);
    mac_via_write(&via, 4, 100);
    mac_via_write(&via, 5, 0);
    TEST_ASSERT(!mac_via_irq(&via));

    mac_via_advance(&via, 50);
    TEST_ASSERT_EQ_INT(50, mac_via_read(&via, 4));
    TEST_ASSERT(!mac_via_irq(&via));

    mac_via_advance(&via, 60);
    TEST_ASSERT(mac_via_irq(&via));
    TEST_ASSERT(mac_via_read(&via, 13) & 0x80);

    /* Reading the low counter byte clears the flag. */
    mac_via_read(&via, 4);
    TEST_ASSERT(!mac_via_irq(&via));

    /* A masked source does not raise the IRQ line but shows in IFR. */
    mac_via_set_flag(&via, VIA_INT_CA1);
    TEST_ASSERT(!mac_via_irq(&via));
    TEST_ASSERT(mac_via_read(&via, 13) & VIA_INT_CA1);
    mac_via_write(&via, 13, VIA_INT_CA1);
    TEST_ASSERT(!(mac_via_read(&via, 13) & VIA_INT_CA1));
}

static void test_via_ports(void)
{
    mac_via_t via = { 0 };

    via.in_a = 0xFF;
    mac_via_write(&via, 3, 0x0F);       /* low nibble output */
    mac_via_write(&via, 1, 0x05);
    TEST_ASSERT_EQ_INT(0xF5, mac_via_read(&via, 1));
}

static void test_iwm_mode_readback(void)
{
    mac_iwm_t iwm = { 0 };

    /* Lines 6 (Q6) and 7 (Q7) on, then write the mode register. */
    mac_iwm_access(&iwm, 13 * 0x200, false, 0);
    mac_iwm_access(&iwm, 15 * 0x200, true, 0x17);
    /* Q7 off: status register shows the mode bits and the sense line. */
    TEST_ASSERT_EQ_INT(0x80 | 0x17, mac_iwm_access(&iwm, 14 * 0x200, false, 0));
}

static void test_rom_patch_rejects_other_roms(void)
{
    static uint8_t other[0x80000];

    TEST_ASSERT_EQ_INT(-1, mac_rom_patch(other, sizeof other));
    TEST_ASSERT_EQ_INT(-1, mac_rom_patch(other, 0x1000));
}

/* Shut Down: the ROM sets DDRB bit 2 (offset 0x400) and clears port B bit 2 of the RBV (P10 D5). */
static void test_power_off_request(void)
{
    TEST_ASSERT_EQ_INT(0, mac_init(&mac, ram, RAM_SIZE, rom, ROM_SIZE));
    mac_reset(&mac);
    TEST_ASSERT_EQ_INT(MAC_POWER_NONE, mac.power_request);
    mac_bus_write(&mac, 0x50F26400, 1, 0x04);       /* the data direction: not a request */
    TEST_ASSERT_EQ_INT(MAC_POWER_NONE, mac.power_request);
    mac_bus_write(&mac, 0x50F26000, 1, 0x04);       /* power stays on */
    TEST_ASSERT_EQ_INT(MAC_POWER_NONE, mac.power_request);
    mac_bus_write(&mac, 0x50F26000, 1, 0x00);
    TEST_ASSERT_EQ_INT(MAC_POWER_OFF, mac.power_request);

    /* mac_run does nothing more once the Mac asked. */
    uint32_t before = (uint32_t)mac.insn_count;

    mac_run(&mac, 1000);
    TEST_ASSERT_EQ_INT(before, (uint32_t)mac.insn_count);

    mac_reset(&mac);
    TEST_ASSERT_EQ_INT(MAC_POWER_NONE, mac.power_request);
}

static void test_rbv_registers(void)
{
    mac_rbv_t rbv = { 0 };

    /* Monitor sense (13" RGB = 6, set by mac_reset) in bits 5-3, depth in bits 2-0. */
    rbv.sense = RBV_SENSE_13_RGB;
    mac_rbv_write(&rbv, 0x10, 3);
    TEST_ASSERT_EQ_INT((6 << 3) | 3, mac_rbv_read(&rbv, 0x10, false));

    /* Slot flags are active low: bit 6 is 1 at rest, 0 while blanked. */
    TEST_ASSERT(mac_rbv_read(&rbv, 0x02, false) & 0x40);
    TEST_ASSERT(!(mac_rbv_read(&rbv, 0x02, true) & 0x40));

    /* The video interrupt needs both enables; the ISR re-arms it. */
    mac_rbv_vblank(&rbv);
    TEST_ASSERT(!mac_rbv_irq(&rbv));
    mac_rbv_write(&rbv, 0x12, 0xC0);        /* slot enable: video */
    mac_rbv_write(&rbv, 0x13, 0x82);        /* enable: slot interrupts */
    mac_rbv_write(&rbv, 0x12, 0xC0);
    mac_rbv_vblank(&rbv);
    TEST_ASSERT(mac_rbv_irq(&rbv));
    TEST_ASSERT(mac_rbv_read(&rbv, 0x03, false) & 0x82);
    mac_rbv_write(&rbv, 0x12, 0xC0);        /* what the ISR does */
    TEST_ASSERT(!mac_rbv_irq(&rbv));

    /* Clearing the enable bit stops it. */
    mac_rbv_vblank(&rbv);
    TEST_ASSERT(mac_rbv_irq(&rbv));
    mac_rbv_write(&rbv, 0x12, 0x40);
    TEST_ASSERT(!mac_rbv_irq(&rbv));
}

static void test_dac_and_render(void)
{
    static uint8_t vram[0x80000];
    static uint32_t pixels[MAC_SCREEN_W * MAC_SCREEN_H];

    TEST_ASSERT_EQ_INT(0, mac_init(&mac, ram, RAM_SIZE, rom, ROM_SIZE));
    mac_reset(&mac);
    mac.vram = vram;
    mac.vram_size = sizeof vram;
    mac.vram_base = 0xFBB08000u;

    /* Entry 5 = (1, 2, 3); the index moves on after the blue byte. */
    mac_bus_write(&mac, 0x50F24000, 1, 5);
    mac_bus_write(&mac, 0x50F24004, 1, 1);
    mac_bus_write(&mac, 0x50F24004, 1, 2);
    mac_bus_write(&mac, 0x50F24004, 1, 3);
    TEST_ASSERT_EQ_INT(6, mac_bus_read(&mac, 0x50F24000, 1));
    TEST_ASSERT_EQ_INT(3, mac.dac.rgb[5][2]);

    /* 8 bits per pixel: the first byte is pixel (0, 0). */
    mac.rbv.depth = 3;
    mac_bus_write(&mac, 0xFBB08000u, 1, 5);
    TEST_ASSERT_EQ_INT(0, mac_render(&mac, pixels));
    TEST_ASSERT_EQ_INT(0x010203, pixels[0]);

    /* 1 bit per pixel: the top bit of a byte is the leftmost pixel. */
    mac.rbv.depth = 0;
    mac.dac.rgb[255][0] = 0xFF;
    mac_bus_write(&mac, 0xFBB08000u, 1, 0x80);
    TEST_ASSERT_EQ_INT(0, mac_render(&mac, pixels));
    TEST_ASSERT_EQ_INT(0xFF0000, pixels[0]);
    TEST_ASSERT_EQ_INT(0, pixels[1]);

    /* Video RAM reads back through the bus. */
    TEST_ASSERT_EQ_INT(0x80, mac_bus_read(&mac, 0xFBB08000u, 1));
}

static void test_scsi_bus_is_empty(void)
{
    mac_scsi_t scsi = { { 0 } };

    /* Arbitrate: the chip reports "arbitration in progress". */
    mac_scsi_access(&scsi, 0x20, true, 0x01);
    TEST_ASSERT(mac_scsi_access(&scsi, 0x10, false, 0) & 0x40);
    /* Select a target: BSY never comes back. */
    mac_scsi_access(&scsi, 0x10, true, 0x04);
    TEST_ASSERT(!(mac_scsi_access(&scsi, 0x40, false, 0) & 0x40));
}

/* The disk driver needs a disk, a drive record and the guest's registers; the
 * handlers are called as the emulated opcodes would call them. */
static uint8_t disk_image[4 * 512];

static int test_disk_read(void *ctx, uint32_t offset, void *buf, uint32_t len)
{
    (void)ctx;
    if (offset + len > sizeof disk_image)
    {
        return -1;
    }
    memcpy(buf, disk_image + offset, len);
    return 0;
}

static int test_disk_write(void *ctx, uint32_t offset, const void *buf,
                           uint32_t len)
{
    (void)ctx;
    if (offset + len > sizeof disk_image)
    {
        return -1;
    }
    memcpy(disk_image + offset, buf, len);
    return 0;
}

static void test_disk_driver(void)
{
    const uint32_t pb = 0x1000;     /* parameter block in guest RAM */
    const uint32_t dce = 0x2000;
    const uint32_t buf = 0x3000;

    for (unsigned i = 0; i < sizeof disk_image; i++)
    {
        disk_image[i] = (uint8_t)(i * 7);
    }
    memset(ram, 0, sizeof ram);
    TEST_ASSERT_EQ_INT(0, mac_init(&mac, ram, RAM_SIZE, rom, ROM_SIZE));
    mac.disk.read = test_disk_read;
    mac.disk.write = test_disk_write;
    mac.disk.blocks = 4;
    mac_reset(&mac);
    mac.overlay = false;            /* as the ROM does once it runs */

    /* Install + Open: the drive record describes the disk. */
    m68k_vm_set_reg(M68K_VM_REG_A0 + 1, dce);
    mac_disk_install(&mac);
    TEST_ASSERT_EQ_INT(MAC_STUB_BASE + MAC_STUB_DRIVER,
                       mac_bus_read(&mac, dce, 4));
    mac_disk_open(&mac);
    mac_disk_next(&mac);
    TEST_ASSERT_EQ_INT(1, m68k_vm_get_reg(M68K_VM_REG_D0 + 1));
    TEST_ASSERT_EQ_INT(8 << 16 | 0xFFCE, m68k_vm_get_reg(M68K_VM_REG_D0));
    TEST_ASSERT_EQ_INT(MAC_STUB_BASE + MAC_STUB_DRIVE_RECORD + 6,
                       m68k_vm_get_reg(M68K_VM_REG_A0));
    TEST_ASSERT_EQ_INT(4, mac_bus_read(&mac, MAC_STUB_BASE +
                                       MAC_STUB_DRIVE_RECORD + 18, 2));

    /* Prime: read blocks 1-2 into guest memory (A002 = _Read). */
    m68k_vm_set_reg(M68K_VM_REG_A0, pb);
    m68k_vm_set_reg(M68K_VM_REG_A0 + 1, dce);
    mac_bus_write(&mac, pb + 6, 2, 0xA002);
    mac_bus_write(&mac, pb + 22, 2, 8);
    mac_bus_write(&mac, pb + 32, 4, buf);
    mac_bus_write(&mac, pb + 36, 4, 1024);
    mac_bus_write(&mac, dce + 16, 4, 512);
    mac_disk_prime(&mac);
    TEST_ASSERT_EQ_INT(0, m68k_vm_get_reg(M68K_VM_REG_D0));
    TEST_ASSERT_EQ_INT(1024, mac_bus_read(&mac, pb + 40, 4));
    TEST_ASSERT_EQ_INT(1536, mac_bus_read(&mac, dce + 16, 4));
    TEST_ASSERT_EQ_INT(disk_image[512], ram[buf]);
    TEST_ASSERT_EQ_INT(disk_image[1535], ram[buf + 1023]);

    /* Reading past the end, odd lengths and other drives fail. */
    mac_bus_write(&mac, dce + 16, 4, 3 * 512);
    mac_disk_prime(&mac);
    TEST_ASSERT_EQ_INT(-19, (int16_t)m68k_vm_get_reg(M68K_VM_REG_D0));
    mac_bus_write(&mac, pb + 36, 4, 100);
    mac_disk_prime(&mac);
    TEST_ASSERT_EQ_INT(-50, (int16_t)m68k_vm_get_reg(M68K_VM_REG_D0));
    mac_bus_write(&mac, pb + 22, 2, 3);
    mac_disk_prime(&mac);
    TEST_ASSERT_EQ_INT(-56, (int16_t)m68k_vm_get_reg(M68K_VM_REG_D0));

    /* Write block 0 (A003 = _Write). */
    mac_bus_write(&mac, pb + 6, 2, 0xA003);
    mac_bus_write(&mac, pb + 22, 2, 8);
    mac_bus_write(&mac, pb + 36, 4, 512);
    mac_bus_write(&mac, dce + 16, 4, 0);
    mac_bus_write(&mac, buf, 1, 0xEE);
    mac_disk_prime(&mac);
    TEST_ASSERT_EQ_INT(0, m68k_vm_get_reg(M68K_VM_REG_D0));
    TEST_ASSERT_EQ_INT(0xEE, disk_image[0]);

    /* Control and Status. */
    mac_bus_write(&mac, pb + 26, 2, 24);        /* partition size */
    mac_disk_control(&mac);
    TEST_ASSERT_EQ_INT(0, m68k_vm_get_reg(M68K_VM_REG_D0));
    TEST_ASSERT_EQ_INT(4, mac_bus_read(&mac, pb + 28, 4));
    mac_bus_write(&mac, pb + 26, 2, 99);
    mac_disk_control(&mac);
    TEST_ASSERT_EQ_INT(-17, (int16_t)m68k_vm_get_reg(M68K_VM_REG_D0));
    mac_bus_write(&mac, pb + 26, 2, 8);         /* drive status */
    mac_disk_status(&mac);
    TEST_ASSERT_EQ_INT(0, m68k_vm_get_reg(M68K_VM_REG_D0));
    TEST_ASSERT_EQ_INT(8, mac_bus_read(&mac, pb + 28 + 3, 1));  /* in place */
    TEST_ASSERT_EQ_INT(8, mac_bus_read(&mac, pb + 28 + 12, 2)); /* drive # */
}

static uint8_t second_image[2 * 512];

static int second_read(void *ctx, uint32_t offset, void *buf, uint32_t len)
{
    (void)ctx;
    if (offset + len > sizeof second_image)
        return -1;
    memcpy(buf, second_image + offset, len);
    return 0;
}

static int second_write(void *ctx, uint32_t offset, const void *buf, uint32_t len)
{
    (void)ctx;
    if (offset + len > sizeof second_image)
        return -1;
    memcpy(second_image + offset, buf, len);
    return 0;
}

/* Several drives: each gets its own drive number, record, size and data. */
static void test_disk_driver_two_drives(void)
{
    const uint32_t pb = 0x1000;
    const uint32_t dce = 0x2000;
    const uint32_t buf = 0x3000;

    for (unsigned i = 0; i < sizeof second_image; i++)
        second_image[i] = (uint8_t)(i * 3 + 1);
    memset(ram, 0, sizeof ram);
    TEST_ASSERT_EQ_INT(0, mac_init(&mac, ram, RAM_SIZE, rom, ROM_SIZE));
    mac.disk.read = test_disk_read;
    mac.disk.write = test_disk_write;
    mac.disk.blocks = 4;
    mac.more_disks[0].read = second_read;
    mac.more_disks[0].write = second_write;
    mac.more_disks[0].blocks = 2;
    mac.more_disks[1].blocks = 0;               /* a gap: drive 10 is absent */
    mac.more_disks[2].read = second_read;
    mac.more_disks[2].write = second_write;
    mac.more_disks[2].blocks = 2;
    mac.more_disks[2].read_only = true;
    mac_reset(&mac);
    mac.overlay = false;

    m68k_vm_set_reg(M68K_VM_REG_A0 + 1, dce);
    mac_disk_open(&mac);

    static const uint32_t want_drive[3] = {8, 9, 11};
    static const uint32_t want_blocks[3] = {4, 2, 2};

    for (int i = 0; i < 3; i++)
    {
        mac_disk_next(&mac);
        TEST_ASSERT_EQ_INT(1, m68k_vm_get_reg(M68K_VM_REG_D0 + 1));
        TEST_ASSERT_EQ_INT(want_drive[i] << 16 | 0xFFCE,
                           m68k_vm_get_reg(M68K_VM_REG_D0));

        uint32_t rec = m68k_vm_get_reg(M68K_VM_REG_A0) - 6;

        TEST_ASSERT_EQ_INT(want_drive[i], mac_bus_read(&mac, rec + 12, 2));
        TEST_ASSERT_EQ_INT(want_blocks[i], mac_bus_read(&mac, rec + 18, 2));
    }
    mac_disk_next(&mac);
    TEST_ASSERT_EQ_INT(0, m68k_vm_get_reg(M68K_VM_REG_D0 + 1));

    /* Reads and writes go to the drive named in ioVRefNum. */
    m68k_vm_set_reg(M68K_VM_REG_A0, pb);
    m68k_vm_set_reg(M68K_VM_REG_A0 + 1, dce);
    mac_bus_write(&mac, pb + 6, 2, 0xA002);
    mac_bus_write(&mac, pb + 22, 2, 9);
    mac_bus_write(&mac, pb + 32, 4, buf);
    mac_bus_write(&mac, pb + 36, 4, 512);
    mac_bus_write(&mac, dce + 16, 4, 512);
    mac_disk_prime(&mac);
    TEST_ASSERT_EQ_INT(0, m68k_vm_get_reg(M68K_VM_REG_D0));
    TEST_ASSERT_EQ_INT(second_image[512], ram[buf]);

    mac_bus_write(&mac, pb + 22, 2, 10);        /* the gap */
    mac_disk_prime(&mac);
    TEST_ASSERT_EQ_INT(-56, (int16_t)m68k_vm_get_reg(M68K_VM_REG_D0));

    mac_bus_write(&mac, pb + 6, 2, 0xA003);     /* write to the read-only drive 11 */
    mac_bus_write(&mac, pb + 22, 2, 11);
    mac_bus_write(&mac, dce + 16, 4, 0);
    mac_disk_prime(&mac);
    TEST_ASSERT_EQ_INT(-44, (int16_t)m68k_vm_get_reg(M68K_VM_REG_D0));

    mac_bus_write(&mac, pb + 22, 2, 9);         /* status and size of drive 9 */
    mac_bus_write(&mac, pb + 26, 2, 24);
    mac_disk_control(&mac);
    TEST_ASSERT_EQ_INT(2, mac_bus_read(&mac, pb + 28, 4));
    mac_bus_write(&mac, pb + 26, 2, 8);
    mac_disk_status(&mac);
    TEST_ASSERT_EQ_INT(9, mac_bus_read(&mac, pb + 28 + 12, 2));
}

static void test_stub_window(void)
{
    TEST_ASSERT_EQ_INT(0, mac_init(&mac, ram, RAM_SIZE, rom, ROM_SIZE));
    /* The install routine starts with movem.l and is readable... */
    TEST_ASSERT_EQ_INT(0x48E7, mac_bus_read(&mac, MAC_STUB_BASE, 2));
    /* ...and the data part is writable. */
    mac_bus_write(&mac, MAC_STUB_BASE + MAC_STUB_DRIVE_RECORD, 4, 0x12345678);
    TEST_ASSERT_EQ_INT(0x12345678,
                       mac_bus_read(&mac, MAC_STUB_BASE + MAC_STUB_DRIVE_RECORD, 4));
}

static void test_input_packets(void)
{
    TEST_ASSERT_EQ_INT(0, mac_init(&mac, ram, RAM_SIZE, rom, ROM_SIZE));
    mac_reset(&mac);

    /* A mouse move is split into packets of at most 63 pixels; the button is
     * bit 7 of the first byte and is 1 when released. */
    mac_mouse_move(&mac, 100, -5, false);
    TEST_ASSERT_EQ_INT(2, mac.input_count);
    TEST_ASSERT_EQ_INT(3, mac.input[0][0]);
    TEST_ASSERT_EQ_INT(0x80 | (-5 & 0x7F), mac.input[0][1]);
    TEST_ASSERT_EQ_INT(0x80 | 63, mac.input[0][2]);
    TEST_ASSERT_EQ_INT(0x80 | 37, mac.input[1][2]);
    TEST_ASSERT_EQ_INT(0x80, mac.input[1][1]);

    mac_mouse_move(&mac, 0, 0, true);
    TEST_ASSERT_EQ_INT(0x00, mac.input[2][1]);

    /* A key packet: bit 7 of the code = released, second key = none. */
    mac_key(&mac, 0x11, true);
    mac_key(&mac, 0x11, false);
    TEST_ASSERT_EQ_INT(2, mac.input[3][0]);
    TEST_ASSERT_EQ_INT(0x11, mac.input[3][1]);
    TEST_ASSERT_EQ_INT(0xFF, mac.input[3][2]);
    TEST_ASSERT_EQ_INT(0x91, mac.input[4][1]);

    /* Nothing is injected before the ADB Manager has set up its table. */
    TEST_ASSERT(!mac_adb_ready(&mac, 3));

    /* ADBBase ($CF8) -> table at 0x2000: device 2 without a handler,
     * device 3 with one. Low RAM is only writable once the ROM overlay
     * at address 0 is off. */
    mac.overlay = false;
    mac_bus_write(&mac, 0xCF8, 4, 0x2000);
    mac_bus_write(&mac, 0x2000 + 2, 1, 2);
    mac_bus_write(&mac, 0x2000 + 4, 4, 0);
    mac_bus_write(&mac, 0x2000 + 12 + 2, 1, 3);
    mac_bus_write(&mac, 0x2000 + 12 + 4, 4, 0x4000);
    TEST_ASSERT(!mac_adb_ready(&mac, 2));
    TEST_ASSERT(mac_adb_ready(&mac, 3));

    /* A table pointer outside RAM is not trusted. */
    mac_bus_write(&mac, 0xCF8, 4, 0xFFFFFFF0);
    TEST_ASSERT(!mac_adb_ready(&mac, 3));

    /* The queue is bounded. */
    for (int i = 0; i < 200; i++)
    {
        mac_key(&mac, 1, true);
    }
    TEST_ASSERT_EQ_INT(64, mac.input_count);
}

int main(void)
{
    TEST_RUN(test_init_rejects);
    TEST_RUN(test_overlay_and_rom_mirror);
    TEST_RUN(test_run_clears_overlay);
    TEST_RUN(test_unimplemented_io_is_logged);
    TEST_RUN(test_via_timer_and_interrupts);
    TEST_RUN(test_via_ports);
    TEST_RUN(test_iwm_mode_readback);
    TEST_RUN(test_rom_patch_rejects_other_roms);
    TEST_RUN(test_rbv_registers);
    TEST_RUN(test_power_off_request);
    TEST_RUN(test_dac_and_render);
    TEST_RUN(test_scsi_bus_is_empty);
    TEST_RUN(test_disk_driver);
    TEST_RUN(test_disk_driver_two_drives);
    TEST_RUN(test_stub_window);
    TEST_RUN(test_input_packets);

    return TEST_REPORT("mac");
}
