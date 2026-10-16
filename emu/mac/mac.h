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

typedef struct
{
    uint32_t addr;
    uint32_t pc;
    uint32_t value;     /* value written; 0 for reads */
    uint8_t size;
    bool write;
} mac_access_t;

typedef struct
{
    uint32_t pc;
    uint16_t opcode;
} mac_trace_t;

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
    mac_access_t unimpl[MAC_UNIMPL_LOG_SIZE];
    uint32_t unimpl_total;

    uint32_t via1_reg[16];      /* raw register file of VIA1 (see mac.c) */
} mac_t;

/*
 * Attach 'ram' and 'rom' to the model and select it in the CPU core. Returns
 * 0, or -1 for bad arguments (the ROM must be a power of two, >= 64 KiB).
 */
int mac_init(mac_t *mac, uint8_t *ram, uint32_t ram_size,
             const uint8_t *rom, uint32_t rom_size);

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
