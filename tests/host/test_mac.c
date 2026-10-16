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

static void test_rbv_registers(void)
{
    mac_rbv_t rbv = { 0 };

    /* Monitor sense (13" RGB = 6) in bits 5-3, depth in bits 2-0. */
    mac_rbv_write(&rbv, 0x10, 3);
    TEST_ASSERT_EQ_INT((6 << 3) | 3, mac_rbv_read(&rbv, 0x10, false));

    /* Bit 6 of register 2 follows the blanking input. */
    TEST_ASSERT(!(mac_rbv_read(&rbv, 0x02, false) & 0x40));
    TEST_ASSERT(mac_rbv_read(&rbv, 0x02, true) & 0x40);

    /* The vertical blank interrupt needs the enable bit. */
    mac_rbv_set_flag(&rbv, RBV_INT_VBLANK);
    TEST_ASSERT(!mac_rbv_irq(&rbv));
    mac_rbv_write(&rbv, 0x12, 0xC0);
    TEST_ASSERT(mac_rbv_irq(&rbv));
    mac_rbv_write(&rbv, 0x02, 0x40);
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
    TEST_RUN(test_dac_and_render);
    TEST_RUN(test_scsi_bus_is_empty);

    return TEST_REPORT("mac");
}
