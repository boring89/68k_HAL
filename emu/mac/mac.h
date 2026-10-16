/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef EMU_MAC_MAC_H
#define EMU_MAC_MAC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* CPU clock of the Mac IIci (25 MHz); the sound chip and the VIAs count from it. */
#define MAC_CPU_HZ 25000000u

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
/* Slot $E: the on-board video's declaration ROM is the tail of the main ROM. */
#define MAC_SLOT_E_BASE 0xFE000000u
#define MAC_SLOT_E_END 0xFF000000u

/*
 * Native-call stubs (emu/mac/stub.s): a small read-write window in the ROM
 * address range that holds the code and data of the .Disk driver.
 */
#define MAC_STUB_BASE 0x4F000000u
#define MAC_STUB_SIZE 0x2000u
#define MAC_STUB_ADBOP 0x60u
#define MAC_STUB_INJECT 0xA0u       /* offset of the input injection routine */
#define MAC_STUB_PACKET 0x700u      /* the packet handed to the ROM's handler */        /* offset of the ADBOp replacement */
#define MAC_STUB_DRIVER 0x100u      /* offset of the driver header */
#define MAC_STUB_DRIVE_RECORD 0x900u

/* A disk the Mac sees, backed by the host or the kernel. Offsets and lengths
 * are in bytes; both callbacks return 0 on success and -1 on failure. */
typedef struct
{
    int (*read)(void *ctx, uint32_t offset, void *buf, uint32_t len);
    int (*write)(void *ctx, uint32_t offset, const void *buf, uint32_t len);
    void *ctx;
    uint32_t blocks;            /* size in 512-byte blocks; 0 = no disk */
    bool read_only;
} mac_disk_t;

#define MAC_UNIMPL_LOG_SIZE 2048
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

/* RBV: the Mac IIci's replacement for VIA2, with the video control bits. */
typedef struct
{
    bool vblank_latched;        /* the internal video interrupt is pending */
    bool sound_irq;             /* the sound chip asks for data */
    uint8_t sier;               /* slot interrupt enable (register 0x12) */
    uint8_t rier;               /* interrupt enable (register 0x13) */
    uint8_t depth;              /* bits 0-2 of register 0x10: 0=1 bpp .. 3=8 bpp */
    uint8_t other[0x20];        /* registers without special behavior */
} mac_rbv_t;

/* Apple Sound Chip, FIFO mode only (see asc.c). */
typedef struct
{
    uint8_t mode, control, fifo_mode, status, wave_control, volume, clock;
    uint8_t other[0x80];
    uint8_t wave[0x800];        /* wave table RAM */
    uint8_t fifo[2][0x400];
    uint16_t fifo_head[2];
    uint16_t fifo_count[2];
    uint32_t phase;             /* fraction of a sample period passed (units of 1/CPU_HZ) */
} mac_asc_t;

/* Bt-style color lookup table of the built-in video (the DAC). */
typedef struct
{
    uint8_t rgb[256][3];
    uint8_t index;
    uint8_t phase;              /* next component of the entry: 0=R 1=G 2=B */
} mac_dac_t;

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
    uint32_t break_lo, break_hi;    /* stop before running an instruction here */
    bool break_hit;
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
    uint32_t clk_log[1024];      /* first clock chip commands: cmd << 8 | data, bit 31 = read */
    uint32_t clk_log_len;
    uint32_t rtc_seconds;       /* seconds since 1 Jan 1904, set by the host */

    /* mac_sysbeep: the CPU state to restore when the injected call returns. */
    bool call_active, call_returned;
    uint32_t call_return_pc;
    uint32_t call_regs[20];

    uint8_t stub[MAC_STUB_SIZE];
    mac_disk_t disk;            /* set by the host after mac_init() */

    /* Queued keyboard and mouse packets (see mac_mouse_move, mac_key). */
    uint8_t input[64][4];       /* device (2 = keyboard, 3 = mouse), 2 data bytes */
    uint32_t input_head, input_count;
    bool injecting;
    int mouse_x, mouse_y;       /* where the model thinks the pointer is */
    mac_iwm_t iwm;
    mac_scsi_t scsi;
    mac_rbv_t rbv;
    mac_dac_t dac;
    mac_asc_t asc;
    /*
     * Sound output: called with the unsigned 8-bit mono samples the sound chip
     * played since the last call (a few hundred at a time) and their rate in
     * Hz. NULL = discard.
     */
    void (*audio)(void *ctx, const uint8_t *samples, uint32_t count, uint32_t rate);
    void *audio_ctx;
    uint8_t *vram;              /* video RAM, set by the host */
    uint32_t vram_size;
    uint32_t vram_base;         /* guest address of the first byte */

    mac_via_t via1;
    uint32_t via_rem;           /* CPU cycles not yet given to the VIAs (< 32) */
    uint32_t frame_cycles;      /* CPU cycles since the last 60 Hz tick */
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
#define MAC_EMUL_OP_PATCH_BOOT_GLOBS 0x7107
#define MAC_EMUL_OP_INSTALL_DRIVERS 0x710A
#define MAC_EMUL_OP_INPUT_POP 0x7114
#define MAC_EMUL_OP_DISK_OPEN 0x7110
#define MAC_EMUL_OP_DISK_PRIME 0x7111
#define MAC_EMUL_OP_DISK_CONTROL 0x7112
#define MAC_EMUL_OP_DISK_STATUS 0x7113
#define MAC_EMUL_OP_SCSI_DISPATCH 0x7128

/* Native handlers of the .Disk driver (emu/mac/disk.c). They work on the
 * guest state of the emulated opcode that called them. */
void mac_disk_install(mac_t *mac);
void mac_disk_open(mac_t *mac);
void mac_disk_prime(mac_t *mac);
void mac_disk_control(mac_t *mac);
void mac_disk_status(mac_t *mac);

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

/* RBV registers: 'reg' is the byte offset in the chip, 0-31. */
uint32_t mac_rbv_read(mac_rbv_t *rbv, uint32_t reg, bool vblank);
void mac_rbv_write(mac_rbv_t *rbv, uint32_t reg, uint32_t value);
bool mac_rbv_irq(const mac_rbv_t *rbv);
/* The sound chip's interrupt line (RBV interrupt flag bit 4). */
void mac_rbv_set_sound_irq(mac_rbv_t *rbv, bool asserted);
/* A video frame starts: the internal video interrupt becomes pending (if
 * enabled). */
void mac_rbv_vblank(mac_rbv_t *rbv);
#define RBV_SENSE_13_RGB 6      /* Apple 13" RGB, 640x480 */

/* DAC registers: the address register at offset 0, data at offset 4. */
uint32_t mac_dac_access(mac_dac_t *dac, uint32_t offset, bool write,
                        uint32_t value);

/* Draw the screen into 'out' (width * height 0x00RRGGBB pixels) from the
 * video RAM, depth and color table. Returns 0, or -1 if there is no video
 * RAM. */
#define MAC_SCREEN_W 640
#define MAC_SCREEN_H 480
int mac_render(const mac_t *mac, uint32_t *out);

/* ASC registers: one byte access at 'offset' from 0x50F14000. */
uint32_t mac_asc_access(mac_asc_t *asc, uint32_t offset, bool write, uint32_t value);
bool mac_asc_irq(const mac_asc_t *asc);
uint32_t mac_asc_rate(const mac_asc_t *asc);
/* Let the chip play for 'cycles' CPU cycles; calls 'audio' with what it played. */
void mac_asc_advance(mac_asc_t *asc, uint32_t cycles,
                     void (*audio)(void *ctx, const uint8_t *samples, uint32_t count, uint32_t rate),
                     void *ctx);

/* SCSI registers: one byte access at 'offset' from 0x50F10000. */
uint32_t mac_scsi_access(mac_scsi_t *scsi, uint32_t offset, bool write,
                         uint32_t value);

/* IWM registers: one byte access at 'offset' from 0x50F16000. */
uint32_t mac_iwm_access(mac_iwm_t *iwm, uint32_t offset, bool write,
                        uint32_t value);

/*
 * Make the running system call SysBeep, as an application would: the CPU
 * state is saved, "move.w #3,-(sp); _SysBeep" runs from the stub window, and
 * the state comes back when it returns (at the start of a later mac_run).
 * For tests of the sound chip; the system may be in the middle of anything,
 * so this is only meant for an idle desktop. Returns 0, or -1 if a call is
 * already under way.
 */
int mac_sysbeep(mac_t *mac);

/* Power-on reset: overlay on, CPU reads SSP/PC from the ROM. */
int mac_reset(mac_t *mac);

/* Run for at most 'cycles' CPU cycles; returns the cycles used. */
int mac_run(mac_t *mac, int cycles);

/* The bus as the CPU sees it (also used by tests). */
uint32_t mac_bus_read(mac_t *mac, uint32_t addr, int size);
void mac_bus_write(mac_t *mac, uint32_t addr, int size, uint32_t value);

/*
 * Mouse and keyboard. There is no ADB hardware; instead the packets the ADB
 * devices would send are queued, and the model calls the ROM's own mouse and
 * keyboard handlers with them like an interrupt does (see stub.s, inject).
 *
 * mac_mouse_move: relative motion in pixels and the button state (changes in
 * the button are sent even without motion). mac_key: an ADB key code (0-0x7F,
 * "Inside Macintosh: Devices", ADB keyboard) and whether it goes down.
 */
void mac_mouse_move(mac_t *mac, int dx, int dy, bool button);
void mac_key(mac_t *mac, uint8_t adb_code, bool down);

/* True once the ROM's ADB Manager has a handler for 'device'; queued input
 * is only injected after that. */
bool mac_adb_ready(mac_t *mac, uint8_t device);

/* Number of unimplemented I/O accesses seen (may exceed the log size). */
uint32_t mac_unimpl_total(const mac_t *mac);

#endif
