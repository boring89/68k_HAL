/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "emu/mac/mac.h"

/*
 * ROM patches for the Mac IIci ROM (checksum 368CADFE).
 *
 * The ROM probes the real chips (VIAs, SCC, SCSI ...) and sizes the RAM
 * itself. Instead of imitating all of that hardware we overwrite a few
 * routines so that they skip the probing and call native code through
 * emulated opcodes. The idea and the choice of places follow Basilisk II's
 * rom_patches.cpp (GPL); offsets were checked against a disassembly of
 * this ROM.
 *
 * All offsets are from the start of the ROM image.
 */

#define ROM_CHECKSUM 0x368CADFE

#define OP_NOP 0x4E71
#define OP_JMP_A5 0x4ED5
#define OP_JMP_ABS_L 0x4EF9

/* Early reset code: "move #$2700,SR; reset; ..." through the first hardware
 * detection. Offset 0xBA is where the ROM carries on with the results. */
#define PATCH_RESET 0x8C
#define RESUME_AFTER_RESET 0x408000BA

/* "jmp GetHardwareInfo" after the resume point: the RESET emulated opcode
 * has already provided its results in the registers. */
#define PATCH_GET_HW_INFO 0xC2

/* "lea ...,A6; jmp InitHW": initialization of the VIAs, SCC and friends. */
#define PATCH_INIT_HW 0xCE
#define INIT_HW_WORDS 4

/* The routine that talks to the clock chip (RTC and parameter RAM) through
 * VIA1 port B. It is entered with a return address in A5, the command in D1
 * and the data in D2, and answers in D2. */
#define PATCH_CLKNOMEM 0xB1E4

/* GetHardwareInfo: "tst.b D2; beq probe". D2 = 0 asks the routine to find the
 * model by poking at the hardware; the emulated opcode makes it ask for the
 * IIci instead, so the table lookup below it answers. */
#define PATCH_HWINFO_TEST 0x2F1E

/* _SwapMMUMode: switches the 68030 PMMU between 24 and 32 bit addressing. We
 * always run in 32-bit mode with translation off, so the trap just reports
 * "32-bit" ("moveq #1,D0; rts"). */
#define PATCH_SWAP_MMU_MODE 0x4BD8
#define OP_MOVEQ_1_D0 0x7001
#define OP_RTS 0x4E75

/* ADB (keyboard and mouse bus) reinitialization waits, with interrupts on,
 * for the VIA shift-register state machine to finish scanning the bus. There
 * is no ADB hardware here, so the wait ("btst #5,(349,A3); bne") is skipped. */
#define PATCH_ADB_WAIT 0xA8E6
#define ADB_WAIT_WORDS 4

/* MMU setup: after loading the root pointer the ROM turns translation on with
 * "pmove (A3),tc" (two places). The 24-bit mapping is never needed, so
 * translation stays off. */
#define PATCH_MMU_ENABLE_1 0x4384
#define PATCH_MMU_ENABLE_2 0x438C

/* InitMMU builds the translation tables unless a flag in its frame says the
 * machine has no use for them: "cmpi.b #1,(-26,A6); bne build". Turn the
 * compare into "cmp.b D0,D0" so the flag always reads as set. */
#define PATCH_INIT_MMU 0x430A
#define OP_CMP_B_D0_D0 0xB000


static void put16(uint8_t *rom, uint32_t off, uint16_t v)
{
    rom[off] = v >> 8;
    rom[off + 1] = (uint8_t)v;
}


static void put32(uint8_t *rom, uint32_t off, uint32_t v)
{
    put16(rom, off, v >> 16);
    put16(rom, off + 2, (uint16_t)v);
}


int mac_rom_patch(uint8_t *rom, uint32_t size)
{
    uint32_t sum;

    if (rom == 0 || size != 0x80000)
    {
        return -1;
    }
    sum = (uint32_t)rom[0] << 24 | rom[1] << 16 | rom[2] << 8 | rom[3];
    if (sum != ROM_CHECKSUM)
    {
        return -1;
    }

    put16(rom, PATCH_RESET, MAC_EMUL_OP_RESET);
    put16(rom, PATCH_RESET + 2, OP_JMP_ABS_L);
    put32(rom, PATCH_RESET + 4, RESUME_AFTER_RESET);

    put16(rom, PATCH_GET_HW_INFO, OP_NOP);
    put16(rom, PATCH_GET_HW_INFO + 2, OP_NOP);

    for (uint32_t i = 0; i < INIT_HW_WORDS; i++)
    {
        put16(rom, PATCH_INIT_HW + 2 * i, OP_NOP);
    }

    put16(rom, PATCH_HWINFO_TEST, MAC_EMUL_OP_HWINFO);
    put16(rom, PATCH_HWINFO_TEST + 2, OP_NOP);

    for (uint32_t i = 0; i < ADB_WAIT_WORDS; i++)
    {
        put16(rom, PATCH_ADB_WAIT + 2 * i, OP_NOP);
    }

    put16(rom, PATCH_INIT_MMU, OP_CMP_B_D0_D0);
    put16(rom, PATCH_INIT_MMU + 2, OP_NOP);
    put16(rom, PATCH_INIT_MMU + 4, OP_NOP);

    put16(rom, PATCH_MMU_ENABLE_1, OP_NOP);
    put16(rom, PATCH_MMU_ENABLE_1 + 2, OP_NOP);
    put16(rom, PATCH_MMU_ENABLE_2, OP_NOP);
    put16(rom, PATCH_MMU_ENABLE_2 + 2, OP_NOP);

    put16(rom, PATCH_SWAP_MMU_MODE, OP_MOVEQ_1_D0);
    put16(rom, PATCH_SWAP_MMU_MODE + 2, OP_RTS);

    put16(rom, PATCH_CLKNOMEM, MAC_EMUL_OP_CLKNOMEM);
    put16(rom, PATCH_CLKNOMEM + 2, OP_JMP_A5);
    return 0;
}
