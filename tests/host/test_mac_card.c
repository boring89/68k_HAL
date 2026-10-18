/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "emu/m68k/vm.h"
#include "emu/mac/mac.h"
#include "emu/mac/nubus_driver_code.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The virtual NuBus video card (emu/mac/nubus_video.c, docs/nubus_video.md). */

#define ROM_SIZE 0x10000
#define RAM_SIZE 0x100000
#define PB 0x1000           /* a parameter block in guest RAM */
#define DCE 0x1100          /* a device control entry */
#define PARAM 0x1200        /* the csParam record */
#define VPBLOCK 0x1300

static uint8_t rom[ROM_SIZE];
static uint8_t ram[RAM_SIZE];
static mac_t mac;
static uint8_t *vram;

static uint32_t rd(uint32_t addr, int size)
{
    return mac_bus_read(&mac, addr, size);
}

static void wr(uint32_t addr, int size, uint32_t value)
{
    mac_bus_write(&mac, addr, size, value);
}

static void setup(uint32_t res)
{
    if (vram == NULL)
        vram = calloc(1, MAC_CARD_VRAM_SIZE);
    memset(rom, 0, sizeof rom);
    TEST_ASSERT_EQ_INT(0, mac_init(&mac, ram, RAM_SIZE, rom, ROM_SIZE));
    TEST_ASSERT_EQ_INT(0, mac_card_attach(&mac, vram, res, 0));
    mac_reset(&mac);
    mac.overlay = false;    /* RAM at 0, for the parameter blocks */
}

/* A driver call as the 68k stub makes it: A0 = parameter block, A1 = DCE; the result comes back in D0. */
static int call(bool control, uint32_t code)
{
    wr(PB + 26, 2, code);
    wr(PB + 28, 4, PARAM);
    m68k_vm_set_reg(M68K_VM_REG_A0, PB);
    m68k_vm_set_reg(M68K_VM_REG_A0 + 1, DCE);
    if (control)
        mac_card_control(&mac);
    else
        mac_card_status(&mac);
    return (int16_t)m68k_vm_get_reg(M68K_VM_REG_D0);
}

/* The CRC of Designing Cards and Drivers, ch. 8: rotate left, add the byte; the CRC field counts as 0. */
static uint32_t crc_of(uint32_t base, uint32_t length)
{
    uint32_t crc = 0;

    for (uint32_t i = 0; i < length; i++)
    {
        uint32_t b = (i >= length - 12 && i < length - 8) ? 0 : rd(base + i, 1);

        crc = ((crc << 1) | (crc >> 31)) + b;
    }
    return crc;
}

static uint32_t entry_target(uint32_t at)
{
    uint32_t offset = rd(at, 4) & 0xFFFFFF;

    if (offset & 0x800000)
        offset |= 0xFF000000u;
    return at + offset;
}

static void test_declaration_rom(void)
{
    setup(0);

    /* The format block at the very top of slot $C: byte lanes, test pattern, revision and format. */
    TEST_ASSERT_EQ_INT(0x0F, rd(0xFCFFFFFFu, 1));
    TEST_ASSERT_EQ_INT(0, rd(0xFCFFFFFEu, 1));
    TEST_ASSERT_EQ_INT((int)0x5A932BC7u, (int)rd(0xFCFFFFFAu, 4));
    TEST_ASSERT_EQ_INT(1, rd(0xFCFFFFF9u, 1));
    TEST_ASSERT_EQ_INT(1, rd(0xFCFFFFF8u, 1));

    uint32_t length = rd(0xFCFFFFF0u, 4);
    uint32_t base = 0xFD000000u - length;

    TEST_ASSERT(length > 100 && length <= MAC_CARD_ROM_MAX);
    TEST_ASSERT_EQ_INT((int)crc_of(base, length), (int)rd(0xFCFFFFF4u, 4));

    /* The directory: the board sResource (1), then the video sResource (0x80), then the end. */
    uint32_t dir = entry_target(0xFCFFFFECu);

    TEST_ASSERT_EQ_INT(0x01, rd(dir, 1));
    TEST_ASSERT_EQ_INT(0x80, rd(dir + 4, 1));
    TEST_ASSERT_EQ_INT(0xFF, rd(dir + 8, 1));

    /* The video sResource's type: display, video, Apple driver interface. */
    uint32_t video = entry_target(dir + 4);
    uint32_t type = entry_target(video);

    TEST_ASSERT_EQ_INT(1, rd(video, 1));
    TEST_ASSERT_EQ_INT(3, rd(type, 2));
    TEST_ASSERT_EQ_INT(1, rd(type + 2, 2));
    TEST_ASSERT_EQ_INT(1, rd(type + 4, 2));

    /* The driver directory's 68020 driver is the assembled nubus_driver.s, after its length. */
    uint32_t e = video;

    while (rd(e, 1) != 4)
        e += 4;
    uint32_t driver = entry_target(entry_target(e));

    TEST_ASSERT_EQ_INT((int)(4 + sizeof mac_nubus_driver_code), (int)rd(driver, 4));
    for (uint32_t i = 0; i < sizeof mac_nubus_driver_code; i++)
        TEST_ASSERT_EQ_INT(mac_nubus_driver_code[i], rd(driver + 4 + i, 1));

    /* The ROM is read-only; other slots stay empty. */
    wr(0xFCFFFFFFu, 1, 0);
    TEST_ASSERT_EQ_INT(0x0F, rd(0xFCFFFFFFu, 1));
    TEST_ASSERT_EQ_INT(0, rd(0xFBFFFFFFu, 1));

    /* Without the card the built-in video sees its 13" monitor; with it, none. */
    TEST_ASSERT_EQ_INT(RBV_SENSE_NONE, (int)((rd(0x50F26010u, 1) >> 3) & 7));

    TEST_ASSERT_EQ_INT(-1, mac_card_attach(&mac, vram, mac_card_resolution_count, 0));
    TEST_ASSERT_EQ_INT(-1, mac_card_attach(&mac, vram, 12, 5));     /* 3840x2160 has no 32 bits */
    TEST_ASSERT_EQ_INT(4, mac_card_find_resolution(1280, 720));
    TEST_ASSERT_EQ_INT(-1, mac_card_find_resolution(1280, 721));
}

static void test_render_changes(void)
{
    static uint32_t pixels[640 * 480];
    static uint8_t changed[480];
    uint32_t w, h;

    setup(0);
    mac_screen_size(&mac, &w, &h);
    TEST_ASSERT_EQ_INT(640, (int)w);
    TEST_ASSERT_EQ_INT(480, (int)h);

    /* Everything is drawn the first time, nothing when nothing changed. */
    TEST_ASSERT_EQ_INT(480, mac_render_changes(&mac, pixels, changed));
    TEST_ASSERT_EQ_INT(0, mac_render_changes(&mac, pixels, changed));

    /* 32 bits per pixel (cscSetMode 0x85): row 10 is at 10 * 2560; xRGB, big-endian. */
    wr(PARAM, 2, MAC_CARD_MODE_1BIT + 5);
    wr(PARAM + 6, 2, 0);
    TEST_ASSERT_EQ_INT(0, call(true, 2));
    TEST_ASSERT_EQ_INT((int)MAC_CARD_VRAM_BASE, (int)rd(PARAM + 8, 4));
    TEST_ASSERT_EQ_INT(480, mac_render_changes(&mac, pixels, changed));

    wr(MAC_CARD_VRAM_BASE + 10 * 2560 + 4, 4, 0x00123456);
    TEST_ASSERT_EQ_INT(0x00123456, (int)rd(MAC_CARD_VRAM_BASE + 10 * 2560 + 4, 4));
    TEST_ASSERT_EQ_INT(1, mac_render_changes(&mac, pixels, changed));
    TEST_ASSERT_EQ_INT(1, changed[10]);
    TEST_ASSERT_EQ_INT(0, changed[9]);
    TEST_ASSERT_EQ_INT(0x123456, (int)pixels[10 * 640 + 1]);

    /* 8 bits: the color table (cscSetEntries, sequential from entry 7). */
    wr(PARAM, 2, MAC_CARD_MODE_1BIT + 3);
    TEST_ASSERT_EQ_INT(0, call(true, 2));
    wr(0x1400, 2, 0);
    wr(0x1402, 2, 0xFF00);
    wr(0x1404, 2, 0x8000);
    wr(0x1406, 2, 0x0100);
    wr(PARAM, 4, 0x1400);
    wr(PARAM + 4, 2, 7);
    wr(PARAM + 6, 2, 0);
    TEST_ASSERT_EQ_INT(0, call(true, 3));
    wr(MAC_CARD_VRAM_BASE, 1, 7);
    mac_render_changes(&mac, pixels, changed);
    TEST_ASSERT_EQ_INT(0xFF8001, (int)pixels[0]);

    /* Direct colors cannot be set in an indexed mode. */
    TEST_ASSERT_EQ_INT(-17, call(true, 8));

    /* 16 bits: 0RRRRRGGGGGBBBBB, each widened to 8 bits. */
    wr(PARAM, 2, MAC_CARD_MODE_1BIT + 4);
    wr(PARAM + 6, 2, 0);
    TEST_ASSERT_EQ_INT(0, call(true, 2));
    wr(MAC_CARD_VRAM_BASE, 2, 0x7C1F);
    mac_render_changes(&mac, pixels, changed);
    TEST_ASSERT_EQ_INT(0xFF00FF, (int)pixels[0]);
}

static void test_resolutions(void)
{
    uint32_t w, h;
    int count = 0;

    setup(0);

    /* cscGetNextResolution from "the first" to "no more". */
    wr(PARAM, 4, 0xFFFFFFFEu);
    while (count < 50)
    {
        TEST_ASSERT_EQ_INT(0, call(false, 17));
        if (rd(PARAM + 4, 4) == 0xFFFFFFFDu)
            break;
        if (count == 12)
        {
            TEST_ASSERT_EQ_INT(3840, (int)rd(PARAM + 8, 4));
            TEST_ASSERT_EQ_INT(2160, (int)rd(PARAM + 12, 4));
        }
        wr(PARAM, 4, rd(PARAM + 4, 4));
        count++;
    }
    TEST_ASSERT_EQ_INT((int)mac_card_resolution_count, count);

    /* cscGetVideoParameters: 1920x1080 (id 0x89) at 32 bits: rowBytes 7680. */
    wr(PARAM, 4, MAC_CARD_RES_ID_BASE + 9);
    wr(PARAM + 4, 2, MAC_CARD_MODE_1BIT + 5);
    wr(PARAM + 6, 4, VPBLOCK);
    TEST_ASSERT_EQ_INT(0, call(false, 18));
    TEST_ASSERT_EQ_INT(7680, (int)rd(VPBLOCK + 4, 2));
    TEST_ASSERT_EQ_INT(1080, (int)rd(VPBLOCK + 10, 2));
    TEST_ASSERT_EQ_INT(1920, (int)rd(VPBLOCK + 12, 2));
    TEST_ASSERT_EQ_INT(32, (int)rd(VPBLOCK + 32, 2));
    TEST_ASSERT_EQ_INT(2, (int)rd(PARAM + 14, 4));

    /* cscSwitchMode to 2560x1440 at 8 bits; the card and the ROM's VPBlocks follow. */
    wr(PARAM, 2, MAC_CARD_MODE_1BIT + 3);
    wr(PARAM + 2, 4, MAC_CARD_RES_ID_BASE + 11);
    wr(PARAM + 6, 2, 0);
    TEST_ASSERT_EQ_INT(0, call(true, 10));
    mac_screen_size(&mac, &w, &h);
    TEST_ASSERT_EQ_INT(2560, (int)w);
    TEST_ASSERT_EQ_INT(1440, (int)h);
    TEST_ASSERT_EQ_INT((int)MAC_CARD_VRAM_BASE, (int)rd(DCE + 42, 4));
    TEST_ASSERT_EQ_INT((int)(MAC_CARD_RES_ID_BASE + 11), (int)rd(PARAM + 2, 4));

    uint32_t length = rd(0xFCFFFFF0u, 4);

    TEST_ASSERT_EQ_INT((int)crc_of(0xFD000000u - length, length), (int)rd(0xFCFFFFF4u, 4));

    /* cscGetCurMode reports it; an unknown resolution id is refused. */
    TEST_ASSERT_EQ_INT(0, call(false, 10));
    TEST_ASSERT_EQ_INT(MAC_CARD_MODE_1BIT + 3, (int)rd(PARAM, 2));
    TEST_ASSERT_EQ_INT((int)(MAC_CARD_RES_ID_BASE + 11), (int)rd(PARAM + 2, 4));
    wr(PARAM + 2, 4, 0x200);
    TEST_ASSERT_EQ_INT(-50, call(true, 10));

    /* 3840x2160 (id 0x8C) stops at 16 bits: no 32-bit parameters, no switch to it, no "Millions" in the ROM. */
    wr(PARAM, 4, MAC_CARD_RES_ID_BASE + 12);
    wr(PARAM + 4, 2, MAC_CARD_MODE_1BIT + 5);
    wr(PARAM + 6, 4, VPBLOCK);
    TEST_ASSERT_EQ_INT(-50, call(false, 18));
    wr(PARAM, 2, MAC_CARD_MODE_1BIT + 5);
    wr(PARAM + 2, 4, MAC_CARD_RES_ID_BASE + 12);
    wr(PARAM + 6, 2, 0);
    TEST_ASSERT_EQ_INT(-50, call(true, 10));
    TEST_ASSERT_EQ_INT(MAC_CARD_MODE_1BIT + 5, mac.card[0].rom[mac.card[0].last_mode_entry]);
    wr(PARAM, 2, MAC_CARD_MODE_1BIT + 4);
    TEST_ASSERT_EQ_INT(0, call(true, 10));
    TEST_ASSERT_EQ_INT(0xFF, mac.card[0].rom[mac.card[0].last_mode_entry]);
    wr(PARAM, 4, MAC_CARD_RES_ID_BASE + 11);
    TEST_ASSERT_EQ_INT(0, call(false, 17));
    TEST_ASSERT_EQ_INT(MAC_CARD_MODE_1BIT + 4, (int)rd(PARAM + 20, 2));
    TEST_ASSERT_EQ_INT(-18, call(false, 99));
}

static void test_vblank_interrupt(void)
{
    setup(0);

    /* Off until the driver opens; then every frame raises slot $C's interrupt (RBV, active low). */
    mac_card_vblank(&mac);
    TEST_ASSERT_EQ_INT(0x08, rd(0x50F26002u, 1) & 0x08);

    m68k_vm_set_reg(M68K_VM_REG_A0 + 1, DCE);
    mac_card_open(&mac);
    wr(0x50F26013u, 1, 0x80 | 0x02);        /* RBV: enable slot interrupts */
    mac_card_vblank(&mac);
    TEST_ASSERT_EQ_INT(0x00, rd(0x50F26002u, 1) & 0x08);
    TEST_ASSERT(mac_rbv_irq(&mac.rbv));

    /* The handler's acknowledge (A1 = its sqParm, the slot) clears it. */
    m68k_vm_set_reg(M68K_VM_REG_A0 + 1, MAC_CARD_SLOT);
    mac_card_irq_ack(&mac);
    TEST_ASSERT_EQ_INT(0x08, rd(0x50F26002u, 1) & 0x08);
    TEST_ASSERT(!mac_rbv_irq(&mac.rbv));

    /* cscSetInterrupt with csMode 1 turns them off. */
    wr(DCE + 40, 1, MAC_CARD_SLOT);         /* dCtlSlot */
    wr(PARAM, 1, 1);
    TEST_ASSERT_EQ_INT(0, call(true, 7));
    mac_card_vblank(&mac);
    TEST_ASSERT(!mac_rbv_irq(&mac.rbv));
}

static void test_two_cards(void)
{
    static uint8_t *vram2;
    uint32_t w, h;

    if (vram2 == NULL)
        vram2 = calloc(1, MAC_CARD_VRAM_SIZE);
    memset(rom, 0, sizeof rom);
    TEST_ASSERT_EQ_INT(0, mac_init(&mac, ram, RAM_SIZE, rom, ROM_SIZE));
    TEST_ASSERT_EQ_INT(0, mac_card_attach(&mac, vram, 0, 0));
    TEST_ASSERT_EQ_INT(0, mac_card_attach(&mac, vram2, 2, 0));
    TEST_ASSERT_EQ_INT(-1, mac_card_attach(&mac, vram2, 2, 0));    /* only $C and $D */
    mac_reset(&mac);
    mac.overlay = false;

    /* Slot $D: its own ROM at the top of 0xFDxxxxxx, its own video RAM at 0xD0000000. */
    TEST_ASSERT_EQ_INT(0x0F, rd(0xFDFFFFFFu, 1));
    TEST_ASSERT_EQ_INT(0x0F, rd(0xFCFFFFFFu, 1));
    vram[0] = 0;
    wr(0xD0000000u, 1, 0x5A);
    TEST_ASSERT_EQ_INT(0x5A, vram2[0]);
    TEST_ASSERT_EQ_INT(0, vram[0]);
    TEST_ASSERT_EQ_INT(0, mac_card_screen_size(&mac, 1, &w, &h));
    TEST_ASSERT_EQ_INT(1024, (int)w);
    TEST_ASSERT_EQ_INT(-1, mac_card_screen_size(&mac, 2, &w, &h));

    /* The driver works on the card of its DCE's slot: switch only the second one. */
    wr(DCE + 40, 1, 0xD);
    wr(PARAM, 2, MAC_CARD_MODE_1BIT + 3);
    wr(PARAM + 2, 4, MAC_CARD_RES_ID_BASE + 1);
    wr(PARAM + 6, 2, 0);
    TEST_ASSERT_EQ_INT(0, call(true, 10));
    TEST_ASSERT_EQ_INT((int)0xD0000000u, (int)rd(PARAM + 8, 4));
    mac_card_screen_size(&mac, 1, &w, &h);
    TEST_ASSERT_EQ_INT(800, (int)w);
    mac_card_screen_size(&mac, 0, &w, &h);
    TEST_ASSERT_EQ_INT(640, (int)w);

    /* Each slot has its own interrupt bit (3 = $C, 4 = $D). */
    m68k_vm_set_reg(M68K_VM_REG_A0 + 1, DCE);
    mac_card_open(&mac);
    mac_card_vblank(&mac);
    TEST_ASSERT_EQ_INT(0x00, rd(0x50F26002u, 1) & 0x10);
    TEST_ASSERT_EQ_INT(0x08, rd(0x50F26002u, 1) & 0x08);
    m68k_vm_set_reg(M68K_VM_REG_A0 + 1, 0xD);
    mac_card_irq_ack(&mac);
    TEST_ASSERT_EQ_INT(0x10, rd(0x50F26002u, 1) & 0x10);
    free(vram2);
    vram2 = NULL;
}

int main(void)
{
    TEST_RUN(test_declaration_rom);
    TEST_RUN(test_render_changes);
    TEST_RUN(test_resolutions);
    TEST_RUN(test_vblank_interrupt);
    TEST_RUN(test_two_cards);
    free(vram);
    return TEST_REPORT("mac_card");
}
