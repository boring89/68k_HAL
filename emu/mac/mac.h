/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef EMU_MAC_MAC_H
#define EMU_MAC_MAC_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Machine model of a Macintosh IIci (68030, no FPU, 32-bit addressing).
 *
 * Think of it as the "motherboard" around the CPU in emu/m68k: it decides
 * which guest address reaches RAM, the ROM or a chip. The model never opens a
 * file or draws a pixel; the program that embeds it hands in the ROM and RAM
 * buffers and reads the results back.
 *
 *   0x00000000 RAM (or the ROM, while the reset "overlay" is on)
 *   0x40000000 ROM, mirrored every rom_size bytes up to 0x4FFFFFFF
 *   0x50000000 I/O chips (VIA1, VIA2/RBV, SCC, SCSI, ASC, SWIM ...)
 *
 * Memory map: "Guide to the Macintosh Family Hardware", 2nd ed., ch. 3 (IIci
 * has ROM at 0x40800000 and I/O at 0x50F00000), and Inside Macintosh: Devices.
 */

#define MAC_ROM_BASE 0x40000000u
#define MAC_ROM_END 0x50000000u
#define MAC_IO_BASE 0x50000000u
#define MAC_IO_END 0x60000000u

#define MAC_UNIMPL_LOG_SIZE 64
#define MAC_TRACE_SIZE 4096
#define MAC_FLOW_SIZE 1024       /* ROM blocks remembered in first-seen order */
#define MAC_FLOW_SHIFT 6         /* block = 64 bytes */

typedef struct
{
    uint32_t addr;
    uint32_t pc;
    uint32_t value;     /* value written; 0 for reads */
    uint8_t size;
    bool write;
    uint32_t count;     /* times this exact access (address, pc, r/w) happened */
} mac_access_t;

typedef struct
{
    uint32_t pc;
    uint16_t opcode;
} mac_trace_t;

/* MOS 6522 VIA (see via.c). */
typedef struct
{
    uint8_t ora, orb, ddra, ddrb;
    uint8_t acr, pcr, ifr, ier, sr;
    uint16_t t1_latch, t2_latch;
    uint16_t t1_counter, t2_counter;
    bool t1_running;            /* T1 has been started and not yet timed out */
    bool t2_running;
    uint8_t in_a, in_b;         /* levels the outside world drives on the pins */
} mac_via_t;

/* NCR 5380 SCSI controller with an empty bus (see scsi.c). */
typedef struct
{
    uint8_t reg[8];             /* values the ROM wrote (read-write ones) */
} mac_scsi_t;

/* Apple IWM floppy controller, reduced to what the ROM's probe needs. */
typedef struct
{
    uint8_t lines;      /* bit n = state of IWM line n (see iwm.c) */
    uint8_t mode;       /* mode register, 5 bits */
} mac_iwm_t;

typedef struct
{
    uint8_t *ram;
    uint32_t ram_size;
    const uint8_t *rom;
    uint32_t rom_size;

    bool overlay;               /* ROM shown at address 0 (set at reset) */

    /* Diagnostics. */
    bool trace_enabled;
    uint32_t insn_count;
    mac_trace_t trace[MAC_TRACE_SIZE];
    uint32_t trace_next;        /* total entries written, ring index = % size */
    uint32_t flow_block[MAC_FLOW_SIZE];     /* ROM block numbers, first-seen order */
    uint32_t flow_insn[MAC_FLOW_SIZE];      /* instruction count at first sight */
    uint32_t flow_count;
    uint32_t pmmu_pc[16];       /* the last PMMU instructions: address, */
    uint16_t pmmu_ext[16];      /* extension word */
    uint32_t pmmu_total;
    mac_access_t unimpl[MAC_UNIMPL_LOG_SIZE];
    uint32_t unimpl_total;      /* all accesses */
    uint32_t unimpl_used;       /* distinct entries in unimpl[] */

    uint8_t xpram[256];         /* parameter RAM kept by the clock chip */
    uint32_t rtc_seconds;       /* seconds since 1 Jan 1904, set by the host */

    mac_iwm_t iwm;
    mac_scsi_t scsi;

    mac_via_t via1;
    uint64_t cycles;            /* CPU cycles since reset */
    uint64_t via_cycles;        /* part of 'cycles' already given to the VIAs */
    uint64_t next_tick;         /* cycle of the next 60 Hz tick */
} mac_t;

/*
 * Attach 'ram' and 'rom' to the model and select it in the CPU core. Returns
 * 0, or -1 for bad arguments (the ROM must be a power of two, >= 64 KiB).
 */
int mac_init(mac_t *mac, uint8_t *ram, uint32_t ram_size,
             const uint8_t *rom, uint32_t rom_size);

/*
 * Patch a private copy of the Mac IIci ROM (checksum 368CADFE) so that it
 * skips the hardware probing and calls into this model instead; see
 * emu/mac/rom_patch.c. 'rom' is modified in place. Returns 0, or -1 if the
 * ROM is not the supported one.
 */
int mac_rom_patch(uint8_t *rom, uint32_t size);

/* Emulated opcodes ($71xx are illegal 68k instructions, so no real code
 * uses them; the patched ROM places them where native code must run). */
#define MAC_EMUL_OP_BASE 0x7100
#define MAC_EMUL_OP_LAST 0x7139
#define MAC_EMUL_OP_RESET 0x7103
#define MAC_EMUL_OP_CLKNOMEM 0x7104
#define MAC_EMUL_OP_HWINFO 0x7105
#define MAC_EMUL_OP_SCSI_DISPATCH 0x7128

/* VIA registers: 'reg' is 0-15. */
uint32_t mac_via_read(mac_via_t *via, uint32_t reg);
void mac_via_write(mac_via_t *via, uint32_t reg, uint32_t value);
/* Advance the timers by 'ticks' VIA clock cycles. */
void mac_via_advance(mac_via_t *via, uint32_t ticks);
/* Raise an interrupt flag (VIA_INT_*). */
void mac_via_set_flag(mac_via_t *via, uint8_t mask);
/* True while the IRQ output is active. */
bool mac_via_irq(const mac_via_t *via);

#define VIA_INT_CA2 0x01
#define VIA_INT_CA1 0x02
#define VIA_INT_SR 0x04
#define VIA_INT_T2 0x20
#define VIA_INT_T1 0x40

/* SCSI registers: one byte access at 'offset' from 0x50F10000. */
uint32_t mac_scsi_access(mac_scsi_t *scsi, uint32_t offset, bool write,
                         uint32_t value);

/* IWM registers: one byte access at 'offset' from 0x50F16000. */
uint32_t mac_iwm_access(mac_iwm_t *iwm, uint32_t offset, bool write,
                        uint32_t value);

/* Power-on reset: overlay on, CPU reads SSP/PC from the ROM. */
int mac_reset(mac_t *mac);

/* Run for at most 'cycles' CPU cycles; returns the cycles used. */
int mac_run(mac_t *mac, int cycles);

/* The bus as the CPU sees it (also used by tests). */
uint32_t mac_bus_read(mac_t *mac, uint32_t addr, int size);
void mac_bus_write(mac_t *mac, uint32_t addr, int size, uint32_t value);

/* Number of unimplemented I/O accesses seen (may exceed the log size). */
uint32_t mac_unimpl_total(const mac_t *mac);

#endif
