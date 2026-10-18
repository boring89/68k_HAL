/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "vbe.h"

#include "vbe_parse.h"

#include "arch/x86/io/io.h"
#include "kernel/lib/k_memory.h"
#include "kernel/memory/heap.h"
#include "kernel/printf/printf.h"
#include "platform/pc/vmap/vmap.h"

#include "x86emu.h"

#include <stdarg.h>
#include <stddef.h>

/*
 * VESA BIOS Extensions through an emulated CPU.
 *
 * On a PC started from a legacy BIOS (or a UEFI firmware's CSM), the graphics card's own ROM, the "video
 * BIOS", knows how to switch the card to any of its modes: a program calls interrupt 0x10 with AX = 0x4Fxx
 * in the processor's 16-bit real mode. The kernel runs in 64-bit (or 32-bit protected) mode and cannot just
 * call it, so it runs the video BIOS's code in a small emulated 8086/386 (x86emu, third_party/x86emu, the
 * emulator X.org used for the same job) with its own copy of the first megabyte of memory: the interrupt
 * table and BIOS data area, the video BIOS at 0xC0000 and the system BIOS at 0xF0000. Port accesses go to
 * the real hardware: that is how the video BIOS programs the card. A UEFI boot without CSM has no video
 * BIOS (nothing at 0xC0000), so all of this reports "not available" there.
 *
 * Calls: 4F00h controller information and mode list, 4F01h mode information, 4F02h set mode (bit 14 =
 * linear framebuffer), 4F15h read the monitor's EDID through DDC. VBE Core 3.0 and VBE/DDC 1.1 (VESA).
 */

#define LOW_SIZE 0x100000u          /* the real-mode address space: 1 MiB */
#define BIOS_DATA_END 0x500u        /* interrupt table and BIOS data area */
#define ROM_START 0xC0000u          /* video BIOS, then other option ROMs and the system BIOS */
#define CONVENTIONAL_END 0xA0000u
#define STACK_TOP 0x7000u           /* our stack: 0000:7000 downwards */
#define RETURN_STUB 0x0700u         /* a HLT instruction the call returns to */
#define BUFFER 0x8000u              /* 0000:8000, the info blocks */
#define INSTRUCTION_LIMIT 50000000u /* a video BIOS call that runs longer than this is stuck */

#define VBE_OK 0x004F

static uint8_t *low;                /* our copy of the first megabyte */
static int state;                   /* 0 = not tried, 1 = available, -1 = not available */
static uint32_t fetches;

/* Memory: everything in the private copy (writes to the VGA window 0xA0000-0xBFFFF are dropped there). */
static u8 mem_rdb(u32 addr)
{
    fetches++;
    if (fetches > INSTRUCTION_LIMIT)
        X86EMU_halt_sys();
    return addr < LOW_SIZE ? low[addr] : 0xFF;
}

static u16 mem_rdw(u32 addr)
{
    return (u16)(mem_rdb(addr) | mem_rdb(addr + 1) << 8);
}

static u32 mem_rdl(u32 addr)
{
    return (u32)mem_rdw(addr) | (u32)mem_rdw(addr + 2) << 16;
}

static void mem_wrb(u32 addr, u8 value)
{
    if (addr < LOW_SIZE)
        low[addr] = value;
}

static void mem_wrw(u32 addr, u16 value)
{
    mem_wrb(addr, (u8)value);
    mem_wrb(addr + 1, (u8)(value >> 8));
}

static void mem_wrl(u32 addr, u32 value)
{
    mem_wrw(addr, (u16)value);
    mem_wrw(addr + 2, (u16)(value >> 16));
}

/* Ports: the real hardware. */
static u8 pio_inb(X86EMU_pioAddr port) { return inb(port); }
static u16 pio_inw(X86EMU_pioAddr port) { return inw(port); }
static u32 pio_inl(X86EMU_pioAddr port) { return inl(port); }
static void pio_outb(X86EMU_pioAddr port, u8 value) { outb(port, value); }
static void pio_outw(X86EMU_pioAddr port, u16 value) { outw(port, value); }
static void pio_outl(X86EMU_pioAddr port, u32 value) { outl(port, value); }

/* x86emu's log output (only used when it is built with DEBUG). */
void printk(const char *fmt, ...)
{
    (void)fmt;
}

typedef struct
{
    uint16_t ax, bx, cx, dx, es, di;
} vbe_regs_t;

/* Run int 0x10 in the emulator: as if the CPU had executed INT 10h at RETURN_STUB - 2. */
static void int10(vbe_regs_t *r)
{
    k_memset(&M, 0, sizeof M);
    M.mem_base = 0;
    M.mem_size = LOW_SIZE;
    M.x86.R_AX = r->ax;
    M.x86.R_BX = r->bx;
    M.x86.R_CX = r->cx;
    M.x86.R_DX = r->dx;
    M.x86.R_ES = r->es;
    M.x86.R_DI = r->di;
    M.x86.R_DS = 0x0040;
    M.x86.R_SS = 0;
    M.x86.R_SP = STACK_TOP;

    /* The interrupt frame: flags, CS, IP of the HLT, then into the handler from the interrupt table. */
    low[RETURN_STUB] = 0xF4;
    M.x86.R_SP -= 2;
    mem_wrw(M.x86.R_SP, 0x0002);
    M.x86.R_SP -= 2;
    mem_wrw(M.x86.R_SP, 0);
    M.x86.R_SP -= 2;
    mem_wrw(M.x86.R_SP, RETURN_STUB);
    M.x86.R_IP = (u16)(low[0x40] | low[0x41] << 8);
    M.x86.R_CS = (u16)(low[0x42] | low[0x43] << 8);
    M.x86.R_FLG = 0x0002;

    fetches = 0;
    X86EMU_exec();

    r->ax = M.x86.R_AX;
    r->bx = M.x86.R_BX;
    r->cx = M.x86.R_CX;
    r->dx = M.x86.R_DX;
    r->es = M.x86.R_ES;
    r->di = M.x86.R_DI;
}

static int setup(void)
{
    static X86EMU_memFuncs mem = {mem_rdb, mem_rdw, mem_rdl, mem_wrb, mem_wrw, mem_wrl};
    static X86EMU_pioFuncs pio = {pio_inb, pio_inw, pio_inl, pio_outb, pio_outw, pio_outl};

    if (state != 0)
        return state > 0 ? 0 : -1;

    state = -1;

    /*
     * The first megabyte, read through a window of its own (a 32-bit kernel leaves physical page 0
     * unmapped, and the ROMs are not RAM). Mapped once: setup() runs once.
     */
    const uint8_t *physical = vmap(0, LOW_SIZE, 0);

    /* A video BIOS starts with the option ROM signature 55 AA at 0xC0000. */
    if (physical == NULL || physical[ROM_START] != 0x55 || physical[ROM_START + 1] != 0xAA)
        return -1;

    low = kmalloc(LOW_SIZE);
    if (low == NULL)
        return -1;

    k_memcpy(low, physical, CONVENTIONAL_END);
    k_memset(low + CONVENTIONAL_END, 0, ROM_START - CONVENTIONAL_END);
    k_memcpy(low + ROM_START, physical + ROM_START, LOW_SIZE - ROM_START);

    /* Interrupt 0x10 must lead into a ROM (the video BIOS, or the system BIOS that hands on to it). */
    uint32_t vector = (uint32_t)(low[0x42] | low[0x43] << 8) << 4 | (uint32_t)(low[0x40] | low[0x41] << 8);

    if (vector < ROM_START)
    {
        kfree(low);
        low = NULL;
        return -1;
    }

    X86EMU_setupMemFuncs(&mem);
    X86EMU_setupPioFuncs(&pio);

    vbe_regs_t r = {.ax = 0x4F00, .es = 0, .di = BUFFER};

    k_memcpy(low + BUFFER, "VBE2", 4);      /* ask for the VBE 2.0+ fields */
    int10(&r);
    if (r.ax != VBE_OK)
    {
        kfree(low);
        low = NULL;
        return -1;
    }
    state = 1;
    return 0;
}

int vbe_available(void)
{
    return setup();
}

int vbe_list_modes(vbe_mode_info_t *modes, int max)
{
    uint16_t version, segment, offset;
    uint32_t kib;
    uint16_t list[256];
    int listed = 0;
    int count = 0;

    if (setup() != 0)
        return -1;

    vbe_regs_t r = {.ax = 0x4F00, .es = 0, .di = BUFFER};

    k_memcpy(low + BUFFER, "VBE2", 4);
    int10(&r);
    if (r.ax != VBE_OK || vbe_parse_info(low + BUFFER, &version, &segment, &offset, &kib) != 0)
        return -1;

    /* The list is a row of mode numbers ending with 0xFFFF; it may live in the info block itself. */
    for (uint32_t at = ((uint32_t)segment << 4) + offset; listed < 256 && at + 1 < LOW_SIZE; at += 2)
    {
        uint16_t mode = (uint16_t)(low[at] | low[at + 1] << 8);

        if (mode == 0xFFFF)
            break;
        list[listed++] = mode;
    }

    for (int i = 0; i < listed && count < max; i++)
    {
        vbe_regs_t m = {.ax = 0x4F01, .cx = list[i], .es = 0, .di = BUFFER};

        k_memset(low + BUFFER, 0, VBE_MODE_INFO_SIZE);
        int10(&m);
        if (m.ax != VBE_OK)
            continue;
        vbe_parse_mode_info(low + BUFFER, list[i], version, &modes[count]);
        if (vbe_mode_usable(&modes[count]))
            count++;
    }
    return count;
}

int vbe_set_mode(const vbe_mode_info_t *mode)
{
    if (setup() != 0)
        return -1;

    /* Bit 14: use the linear framebuffer. */
    vbe_regs_t r = {.ax = 0x4F02, .bx = (uint16_t)(mode->mode | 0x4000)};

    int10(&r);
    return r.ax == VBE_OK ? 0 : -1;
}

int vbe_read_edid(uint8_t *edid)
{
    if (setup() != 0)
        return -1;

    /* 4F15h BL = 1: read EDID block DX = 0 of controller CX = 0 into ES:DI. */
    vbe_regs_t r = {.ax = 0x4F15, .bx = 0x0001, .cx = 0, .dx = 0, .es = 0, .di = BUFFER};

    k_memset(low + BUFFER, 0, EDID_SIZE);
    int10(&r);
    if (r.ax != VBE_OK)
        return -1;
    k_memcpy(edid, low + BUFFER, EDID_SIZE);
    return 0;
}
