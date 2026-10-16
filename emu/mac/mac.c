/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "emu/mac/mac.h"

#include <stddef.h>

#include "emu/m68k/vm.h"
#include "emu/mac/stub_code.h"
#include "kernel/lib/k_memory.h"

#define SR_INT_MASK 0x0700
#define SR_SUPERVISOR 0x2000

static void emul_input_pop(mac_t *mac);
static void start_injection(mac_t *mac, uint32_t pc);

#define SCSI_BASE 0x50F10000u
#define SCSI_SPAN 0x2000u

#define RBV_BASE 0x50F26000u
#define RBV_SPAN 0x2000u
#define DAC_BASE 0x50F24000u
#define DAC_SPAN 0x2000u

#define SCC_BASE 0x50F04000u
#define SCC_SPAN 0x2000u
#define SCC_RR0_IDLE 0x54       /* Tx buffer empty, hunting for a flag, Tx underrun */

#define IWM_BASE 0x50F16000u
#define IWM_SPAN 0x2000u

#define VIA1_BASE 0x50F00000u
#define VIA_SPAN 0x2000u
#define VIA_REG_SHIFT 9
#define VIA_REG_MASK 0xF
#define VIA_REG_ORA 1
#define VIA_REG_DDRA 3

/* CPU 25 MHz (Mac IIci); the VIA clock is 783360 Hz, about CPU / 32. The ROM
 * counts "Ticks" with a 60 Hz interrupt on VIA1 CA1 (Guide to the Macintosh
 * Family Hardware, ch. 3). */
#define CPU_PER_VIA 32u
#define CPU_HZ 25000000u
#define TICK_HZ 60u
#define IRQ_LEVEL_VIA1 1
#define IRQ_LEVEL_RBV 2
#define TIME_SLICE 2000     /* CPU cycles between device updates */

/* Port A bit 4 of VIA1 is the ROM overlay line (vOverlay): the ROM maps
 * itself to address 0 until software drives that pin low. */
#define VIA1_A_OVERLAY 0x10


static uint32_t bswap_read(const uint8_t *p, int size)
{
    uint32_t v = 0;

    for (int i = 0; i < size; i++)
    {
        v = (v << 8) | p[i];
    }
    return v;
}


static void bswap_write(uint8_t *p, int size, uint32_t value)
{
    for (int i = size - 1; i >= 0; i--)
    {
        p[i] = (uint8_t)value;
        value >>= 8;
    }
}


static void log_unimpl(mac_t *mac, uint32_t addr, int size, bool write,
                       uint32_t value)
{
    uint32_t pc = m68k_vm_get_reg(M68K_VM_REG_PC);
    mac_access_t *a;

    /* Reads of empty NuBus slots are expected (the slot manager probes them). */
    if (!write && addr >= 0xF0000000u && addr < MAC_SLOT_E_BASE)
    {
        return;
    }
    mac->unimpl_total++;
    for (uint32_t i = 0; i < mac->unimpl_used; i++)
    {
        a = &mac->unimpl[i];
        if (a->addr == addr && a->pc == pc && a->write == write)
        {
            a->count++;
            return;
        }
    }
    if (mac->unimpl_used >= MAC_UNIMPL_LOG_SIZE)
    {
        return;     /* the log keeps the first distinct accesses */
    }
    a = &mac->unimpl[mac->unimpl_used++];
    a->addr = addr;
    a->pc = pc;
    a->value = value;
    a->size = (uint8_t)size;
    a->write = write;
    a->count = 1;
}


static void via1_update_overlay(mac_t *mac)
{
    /* The line is pulled up; it only goes low when driven as an output. */
    bool driven_low = (mac->via1.ddra & VIA1_A_OVERLAY) &&
                      !(mac->via1.ora & VIA1_A_OVERLAY);

    mac->overlay = !driven_low;
}


/* The first part of every video frame is the vertical blanking interval. */
#define VBLANK_FRACTION 16

static bool in_vblank(const mac_t *mac)
{
    return mac->frame_cycles < CPU_HZ / TICK_HZ / VBLANK_FRACTION;
}


/* Byte access to the I/O area. Returns the read value. */
static uint32_t io_access(mac_t *mac, uint32_t addr, bool write, uint32_t value)
{
    if (addr >= VIA1_BASE && addr < VIA1_BASE + VIA_SPAN)
    {
        uint32_t reg = ((addr - VIA1_BASE) >> VIA_REG_SHIFT) & VIA_REG_MASK;

        if (write)
        {
            mac_via_write(&mac->via1, reg, value);
            if (reg == VIA_REG_ORA || reg == VIA_REG_DDRA)
            {
                via1_update_overlay(mac);
            }
            return 0;
        }
        return mac_via_read(&mac->via1, reg);
    }

    if (addr >= RBV_BASE && addr < RBV_BASE + RBV_SPAN)
    {
        if (write)
        {
            mac_rbv_write(&mac->rbv, addr - RBV_BASE, value);
            return 0;
        }
        return mac_rbv_read(&mac->rbv, addr - RBV_BASE, in_vblank(mac));
    }

    if (addr >= DAC_BASE && addr < DAC_BASE + DAC_SPAN)
    {
        return mac_dac_access(&mac->dac, addr - DAC_BASE, write, value);
    }

    if (addr >= SCC_BASE && addr < SCC_BASE + SCC_SPAN)
    {
        /* A serial controller that is idle and never receives anything:
         * enough for the LocalTalk driver's start-up checks, which only look
         * at the status bits. */
        return write ? 0 : SCC_RR0_IDLE;
    }

    if (addr >= SCSI_BASE && addr < SCSI_BASE + SCSI_SPAN)
    {
        return mac_scsi_access(&mac->scsi, addr - SCSI_BASE, write, value);
    }

    if (addr >= IWM_BASE && addr < IWM_BASE + IWM_SPAN)
    {
        return mac_iwm_access(&mac->iwm, addr - IWM_BASE, write, value);
    }

    log_unimpl(mac, addr, 1, write, value);
    return 0;
}


uint32_t mac_bus_read(mac_t *mac, uint32_t addr, int size)
{
    if (addr >= MAC_STUB_BASE && addr - MAC_STUB_BASE + size <= MAC_STUB_SIZE)
    {
        return bswap_read(mac->stub + (addr - MAC_STUB_BASE), size);
    }

    if (addr >= MAC_ROM_BASE && addr < MAC_ROM_END)
    {
        uint32_t off = (addr - MAC_ROM_BASE) & (mac->rom_size - 1);

        if (off + size > mac->rom_size)
        {
            return 0;
        }
        return bswap_read(mac->rom + off, size);
    }

    if (addr < MAC_ROM_BASE)
    {
        if (mac->overlay)
        {
            uint32_t off = addr & (mac->rom_size - 1);

            return off + size > mac->rom_size ? 0
                                              : bswap_read(mac->rom + off, size);
        }
        if (addr + size <= mac->ram_size && addr + size >= addr)
        {
            return bswap_read(mac->ram + addr, size);
        }
        return 0;
    }

    if (mac->vram != NULL && addr >= mac->vram_base &&
        addr - mac->vram_base + size <= mac->vram_size)
    {
        return bswap_read(mac->vram + (addr - mac->vram_base), size);
    }

    if (addr >= MAC_SLOT_E_BASE && addr < MAC_SLOT_E_END)
    {
        /* The slot manager reads the declaration ROM from the top of the
         * slot's 16 MiB; it is the end of the ROM image, repeated. */
        uint32_t off = (addr - MAC_SLOT_E_BASE) & (mac->rom_size - 1);

        return off + size > mac->rom_size ? 0
                                          : bswap_read(mac->rom + off, size);
    }

    if (addr >= MAC_IO_BASE && addr < MAC_IO_END)
    {
        uint32_t v = 0;

        for (int i = 0; i < size; i++)
        {
            v = (v << 8) | (io_access(mac, addr + i, false, 0) & 0xFF);
        }
        return v;
    }

    log_unimpl(mac, addr, size, false, 0);
    return 0;
}


void mac_bus_write(mac_t *mac, uint32_t addr, int size, uint32_t value)
{
    if (addr < MAC_ROM_BASE)
    {
        if (mac->overlay)
        {
            return;     /* ROM is read-only */
        }
        if (addr + size <= mac->ram_size && addr + size >= addr)
        {
            bswap_write(mac->ram + addr, size, value);
        }
        return;
    }

    if (addr >= MAC_STUB_BASE && addr - MAC_STUB_BASE + size <= MAC_STUB_SIZE)
    {
        bswap_write(mac->stub + (addr - MAC_STUB_BASE), size, value);
        return;
    }

    if (addr >= MAC_ROM_BASE && addr < MAC_ROM_END)
    {
        return;
    }

    if (mac->vram != NULL && addr >= mac->vram_base &&
        addr - mac->vram_base + size <= mac->vram_size)
    {
        bswap_write(mac->vram + (addr - mac->vram_base), size, value);
        return;
    }

    if (addr >= MAC_IO_BASE && addr < MAC_IO_END)
    {
        for (int i = 0; i < size; i++)
        {
            uint32_t shift = (size - 1 - i) * 8;

            io_access(mac, addr + i, true, (value >> shift) & 0xFF);
        }
        return;
    }

    log_unimpl(mac, addr, size, true, value);
}


static uint32_t bus_read(void *ctx, uint32_t addr, int size)
{
    return mac_bus_read(ctx, addr, size);
}


static void bus_write(void *ctx, uint32_t addr, int size, uint32_t value)
{
    mac_bus_write(ctx, addr, size, value);
}


/*
 * UniversalInfo: the ROM's table of Macintosh models, found through the
 * list of offsets at 0x408032C0 (GetHardwareInfo, ROM offset 0x2F18). Field
 * offsets inside an entry, as read from that routine:
 *   +0  offset of the address map   +16 hardware config flags
 *   +18 product kind (word)         +24 address map flags
 *   +28 ROM flags
 */
#define UINFO_LIST 0x408032C4u
#define UINFO_MAP 0
#define UINFO_HWCFG 16
#define UINFO_KIND 18
#define UINFO_MAPFLAGS 24
#define UINFO_ROMFLAGS 28
#define PRODUCT_KIND_IICI 0x0505   /* ID byte 5 (IIci), hardware set 5 */
#define HWCFG_FPU_PRESENT 0x10000000u   /* bit 28 (bit 4 of the byte at $B22) */

#define BOOT_STACK 0x10000u
#define BOOT_GLOBS_SIZE 0x1C
#define RAM_SCRATCH_TOP 0x1000u

/* RESET: what the ROM's hardware detection would have produced. */
static void emul_reset(mac_t *mac)
{
    uint32_t list = UINFO_LIST;
    uint32_t entry = 0;
    uint32_t globs = mac->ram_size - BOOT_GLOBS_SIZE;

    for (;;)
    {
        uint32_t off = m68k_vm_read32(list);

        if (off == 0)
        {
            return;     /* model not found: leave the registers alone */
        }
        if (m68k_vm_read16(list + off + UINFO_KIND) == PRODUCT_KIND_IICI)
        {
            entry = list + off;
            break;
        }
        list += 4;
    }

    /* The skipped VIA setup would have switched the ROM overlay off. */
    mac->via1.ddra = VIA1_A_OVERLAY;
    mac->via1.ora = 0;
    via1_update_overlay(mac);
    for (uint32_t a = mac->ram_size - RAM_SCRATCH_TOP; a < mac->ram_size; a++)
    {
        mac->ram[a] = 0;
    }
    /* Boot globals: RAM bank base and size, end of bank table, terminator. */
    mac_bus_write(mac, globs + 0x00, 4, 0);
    mac_bus_write(mac, globs + 0x04, 4, mac->ram_size);
    mac_bus_write(mac, globs + 0x08, 4, 0xFFFFFFFF);
    mac_bus_write(mac, globs + 0x0C, 4, 0);

    m68k_vm_set_reg(M68K_VM_REG_D0, m68k_vm_read32(entry + UINFO_MAPFLAGS));
    m68k_vm_set_reg(M68K_VM_REG_D0 + 1, m68k_vm_read32(entry + UINFO_ROMFLAGS));
    m68k_vm_set_reg(M68K_VM_REG_D0 + 2,
                    m68k_vm_read32(entry + UINFO_HWCFG) & ~HWCFG_FPU_PRESENT);
    m68k_vm_set_reg(M68K_VM_REG_A0, entry + m68k_vm_read32(entry + UINFO_MAP));
    m68k_vm_set_reg(M68K_VM_REG_A0 + 1, entry);
    m68k_vm_set_reg(M68K_VM_REG_A0 + 6, globs);
    m68k_vm_set_reg(M68K_VM_REG_A0 + 7, BOOT_STACK);
}


/*
 * CLKNOMEM: the clock chip protocol, answered without the chip. The command
 * is in D1: bit 7 = read, bits 6-2 = register. Registers 0-7 are the four
 * bytes of the seconds counter (twice); 8-11 and 16-31 are parameter RAM;
 * the "extended" command (bits 6-3 = 0111) addresses all 256 bytes of it
 * (Inside Macintosh: Devices, "Clock chip"; Guide to the Macintosh Family
 * Hardware, "Real-time clock").
 */
/* Parameter RAM byte 0x8A holds the Memory control panel's addressing mode
 * (bit 0 = 32-bit). The value follows Basilisk II's behavior. */
#define XPRAM_ADDRESSING 0x8A
#define XPRAM_32BIT 0x05

/* Parameter RAM bytes 0xE0-0xE3: the network driver AppleTalk uses (the
 * sResource id and the slot of an Ethernet card; zero = LocalTalk). */
#define XPRAM_NETWORK 0xE0

#define CLK_READ 0x80
#define CLK_EXT_MASK 0x78
#define CLK_EXT 0x38

static void emul_clknomem(mac_t *mac)
{
    uint32_t cmd = m68k_vm_get_reg(M68K_VM_REG_D0 + 1);
    uint32_t data = m68k_vm_get_reg(M68K_VM_REG_D0 + 2);
    bool read = (cmd & CLK_READ) != 0;
    uint8_t *cell = NULL;

    if (mac->clk_log_len < 1024)
    {
        mac->clk_log[mac->clk_log_len++] =
            (read ? 0x80000000u : 0) | ((cmd & 0xFFFF) << 8) | (data & 0xFF);
    }
    uint8_t secs[4];

    if ((cmd & CLK_EXT_MASK) == CLK_EXT)
    {
        cell = &mac->xpram[((cmd & 7) << 5) | ((cmd >> 10) & 0x1F)];
    }
    else
    {
        uint32_t reg = (cmd >> 2) & 0x1F;

        if (reg < 8)
        {
            for (int i = 0; i < 4; i++)
            {
                secs[i] = (uint8_t)(mac->rtc_seconds >> (8 * i));
            }
            if (read)
            {
                data = secs[reg & 3];
            }
        }
        else if (reg >= 0x10 || reg <= 0x0B)
        {
            cell = &mac->xpram[reg];
        }
    }

    if (read && cell >= &mac->xpram[XPRAM_NETWORK] &&
        cell < &mac->xpram[XPRAM_NETWORK + 4] && mac->xpram[XPRAM_NETWORK] == 0 &&
        mac->xpram[XPRAM_NETWORK + 1] == 0)
    {
        /* Fresh parameter RAM selects LocalTalk, whose driver wants the SCC
         * interrupts of a serial port. Select an Ethernet driver (sResource
         * 0xF1 in slot 0xA) instead: it does not exist here, so AppleTalk
         * stays off. Basilisk II returns the same values. */
        static const uint8_t network[4] = { 0x00, 0xF1, 0x00, 0x0A };

        *cell = network[cell - &mac->xpram[XPRAM_NETWORK]];
    }
    if (cell == &mac->xpram[XPRAM_ADDRESSING])
    {
        /* The model only runs in 32-bit mode; whatever the ROM writes here,
         * keep telling it that 32-bit addressing is selected. */
        *cell = XPRAM_32BIT;
    }
    if (cell != NULL)
    {
        if (read)
        {
            data = *cell;
        }
        else
        {
            *cell = (uint8_t)data;
            if (cell == &mac->xpram[XPRAM_ADDRESSING])
            {
                *cell = XPRAM_32BIT;
            }
        }
    }
    m68k_vm_set_reg(M68K_VM_REG_D0 + 2, data & 0xFF);
    m68k_vm_set_reg(M68K_VM_REG_D0 + 1, data & 0xFF);
    m68k_vm_set_reg(M68K_VM_REG_D0, 0);
}


/*
 * SCSI_DISPATCH: the classic SCSI Manager entry ($A815). The stack holds the
 * return address, the selector word, the arguments and, for these Pascal
 * functions, room for the result word (Inside Macintosh: Devices, "SCSI
 * Manager"). Every call succeeds except selecting a target, which fails with
 * scCommErr because nobody answers.
 */
#define SCSI_SEL_SELECT 2
#define SCSI_SEL_SELATN 11
#define SCSI_SEL_COUNT 14
#define SCSI_ERR_COMM 2
#define SR_CCR_MASK 0x1F
#define SR_ZERO 0x04

static void emul_scsi_dispatch(mac_t *mac)
{
    /* Bytes of arguments per selector (reset, get, select, cmd, complete,
     * read, write, inquiry slot, rblind, wblind, stat, selatn, msgin,
     * msgout). */
    static const uint8_t arg_bytes[SCSI_SEL_COUNT] = {
        0, 0, 2, 6, 10, 4, 4, 0, 4, 4, 0, 2, 4, 2,
    };
    uint32_t sp = m68k_vm_get_reg(M68K_VM_REG_A0 + 7);
    uint32_t ret = m68k_vm_read32(sp);
    uint32_t sel = m68k_vm_read16(sp + 4);
    uint32_t args;
    int result = 0;

    if (sel >= SCSI_SEL_COUNT)
    {
        log_unimpl(mac, sp, 2, false, sel);
        sel = 0;
    }
    if (sel == SCSI_SEL_SELECT || sel == SCSI_SEL_SELATN)
    {
        result = SCSI_ERR_COMM;
    }
    args = arg_bytes[sel];
    sp += 6 + args;                 /* now at the result word */
    mac_bus_write(mac, sp, 2, (uint32_t)result);
    m68k_vm_set_reg(M68K_VM_REG_A0 + 7, sp);
    m68k_vm_set_reg(M68K_VM_REG_PC, ret);

    /* Callers test the result with the condition codes (tst.w D0). */
    m68k_vm_set_reg(M68K_VM_REG_D0, (uint32_t)result);
    m68k_vm_set_reg(M68K_VM_REG_SR,
                    (m68k_vm_get_reg(M68K_VM_REG_SR) & ~SR_CCR_MASK) |
                        (result == 0 ? SR_ZERO : 0));
}


/*
 * PATCH_BOOT_GLOBS: the frame at A4 holds MemTop at -20 and the distance
 * from A4 to BufPtr at -12. MemTop is the top of our RAM; BufPtr keeps the
 * ROM's own distance but is rounded down to 4 bytes (the ROM derives heap
 * and stack boundaries from it, and an odd value ends up as an odd boot-block
 * address). Then the displaced "movea.l (-20,A4),A6".
 */
#define BOOT_FRAME_MEMTOP (-20)
#define BOOT_FRAME_BUFDIST (-12)

static void emul_patch_boot_globs(mac_t *mac)
{
    uint32_t a4 = m68k_vm_get_reg(M68K_VM_REG_A0 + 4);
    uint32_t dist = mac_bus_read(mac, a4 + BOOT_FRAME_BUFDIST, 4);
    uint32_t bufptr = (a4 + dist) & ~3u;

    mac_bus_write(mac, a4 + BOOT_FRAME_MEMTOP, 4, mac->ram_size);
    mac_bus_write(mac, a4 + BOOT_FRAME_BUFDIST, 4, bufptr - a4);
    m68k_vm_set_reg(M68K_VM_REG_A0 + 6, mac->ram_size);
}


static void emul_op(mac_t *mac, uint32_t pc, uint16_t op)
{
    switch (op)
    {
    case MAC_EMUL_OP_RESET:
        emul_reset(mac);
        break;
    case MAC_EMUL_OP_HWINFO:
        if ((m68k_vm_get_reg(M68K_VM_REG_D0 + 2) & 0xFF) == 0)
        {
            m68k_vm_set_reg(M68K_VM_REG_D0 + 2, PRODUCT_KIND_IICI);
        }
        break;
    case MAC_EMUL_OP_CLKNOMEM:
        emul_clknomem(mac);
        break;
    case MAC_EMUL_OP_PATCH_BOOT_GLOBS:
        emul_patch_boot_globs(mac);
        break;
    case MAC_EMUL_OP_INPUT_POP:
        emul_input_pop(mac);
        break;
    case MAC_EMUL_OP_INSTALL_DRIVERS:
        mac_disk_install(mac);
        break;
    case MAC_EMUL_OP_DISK_OPEN:
        mac_disk_open(mac);
        break;
    case MAC_EMUL_OP_DISK_PRIME:
        mac_disk_prime(mac);
        break;
    case MAC_EMUL_OP_DISK_CONTROL:
        mac_disk_control(mac);
        break;
    case MAC_EMUL_OP_DISK_STATUS:
        mac_disk_status(mac);
        break;
    case MAC_EMUL_OP_SCSI_DISPATCH:
        emul_scsi_dispatch(mac);
        break;
    default:
        log_unimpl(mac, pc, 2, false, op);
        break;
    }
}


static void insn_hook(void *ctx, uint32_t pc)
{
    mac_t *mac = ctx;

    mac->insn_count++;
    if (mac->break_hi != 0 && pc >= mac->break_lo && pc <= mac->break_hi &&
        !mac->break_hit)
    {
        mac->break_hit = true;
        m68k_vm_stop();
    }
    if (mac->trace_enabled)
    {
        /* Remember which ROM blocks run, in the order they are first used. */
        uint32_t block = pc >> MAC_FLOW_SHIFT;

        if (pc >= MAC_ROM_BASE && pc < MAC_ROM_END &&
            mac->flow_count < MAC_FLOW_SIZE &&
            block != mac->flow_block[mac->flow_count ? mac->flow_count - 1 : 0])
        {
            bool seen = false;

            for (uint32_t i = 0; i < mac->flow_count && !seen; i++)
            {
                seen = mac->flow_block[i] == block;
            }
            if (!seen)
            {
                mac->flow_block[mac->flow_count] = block;
                mac->flow_insn[mac->flow_count++] = mac->insn_count;
            }
        }
        mac_trace_t *t = &mac->trace[mac->trace_next % MAC_TRACE_SIZE];

        t->pc = pc;
        t->opcode = (uint16_t)m68k_vm_read16(pc);
        mac->trace_next++;
    }

    uint16_t opcode = (uint16_t)m68k_vm_read16(pc);

    if ((opcode & 0xFFC0) == 0xF000 && mac->trace_enabled)
    {
        /* PMMU general instruction (coprocessor id 0): remember it. */
        mac->pmmu_pc[mac->pmmu_total % 16] = pc;
        mac->pmmu_ext[mac->pmmu_total % 16] = (uint16_t)m68k_vm_read16(pc + 2);
        mac->pmmu_total++;
    }

    if (mac->input_count > 0 && !mac->injecting)
    {
        uint32_t sr = m68k_vm_get_reg(M68K_VM_REG_SR);

        /* Only where a level 1 interrupt could happen, in supervisor mode,
         * and not inside the stub (it ends with RTE). */
        if ((sr & SR_SUPERVISOR) && (sr & SR_INT_MASK) == 0 &&
            (pc < MAC_STUB_BASE || pc >= MAC_STUB_BASE + MAC_STUB_SIZE))
        {
            start_injection(mac, pc);
            return;
        }
    }

    if (opcode >= MAC_EMUL_OP_BASE && opcode <= MAC_EMUL_OP_LAST)
    {
        emul_op(mac, pc, opcode);
        if (m68k_vm_get_reg(M68K_VM_REG_PC) == pc)
        {
            m68k_vm_set_reg(M68K_VM_REG_PC, pc + 2);
        }
    }
}


int mac_init(mac_t *mac, uint8_t *ram, uint32_t ram_size,
             const uint8_t *rom, uint32_t rom_size)
{
    static const m68k_vm_bus_t bus_template = { bus_read, bus_write, NULL };
    m68k_vm_bus_t bus = bus_template;

    if (mac == NULL || ram == NULL || rom == NULL || ram_size < 8 ||
        rom_size < 0x10000 || (rom_size & (rom_size - 1)) != 0)
    {
        return -1;
    }

    k_memset(mac, 0, sizeof *mac);
    mac->ram = ram;
    mac->ram_size = ram_size;
    mac->rom = rom;
    mac->rom_size = rom_size;
    k_memcpy(mac->stub, mac_stub_code, sizeof mac_stub_code);

    if (m68k_vm_init(ram, ram_size, M68K_VM_CPU_68030) != 0)
    {
        return -1;
    }
    bus.ctx = mac;
    m68k_vm_set_bus(&bus);
    m68k_vm_set_insn_hook(insn_hook, mac);
    m68k_vm_set_aline_traps(false);    /* the ROM has its own trap dispatcher */
    return 0;
}


int mac_reset(mac_t *mac)
{
    k_memset(&mac->via1, 0, sizeof mac->via1);
    mac->via1.in_a = 0xFF;
    mac->via1.in_b = 0xFF;
    mac->via_rem = 0;
    mac->frame_cycles = 0;
    k_memset(&mac->iwm, 0, sizeof mac->iwm);
    k_memset(&mac->scsi, 0, sizeof mac->scsi);
    k_memset(&mac->rbv, 0, sizeof mac->rbv);
    k_memset(&mac->dac, 0, sizeof mac->dac);
    mac->overlay = true;
    mac->insn_count = 0;
    mac->flow_count = 0;
    mac->pmmu_total = 0;
    mac->trace_next = 0;
    mac->unimpl_total = 0;
    mac->unimpl_used = 0;
    mac->input_count = 0;
    mac->injecting = false;
    m68k_vm_reset_cpu();
    mac->insn_count = 0;    /* the reset itself runs one instruction */
    return 0;
}


/* Let time pass for the chips after the CPU ran 'used' cycles. */
static void advance_devices(mac_t *mac, int used)
{
    uint32_t total = mac->via_rem + (uint32_t)used;

    if (total >= CPU_PER_VIA)
    {
        mac_via_advance(&mac->via1, total / CPU_PER_VIA);
    }
    mac->via_rem = total % CPU_PER_VIA;

    mac->frame_cycles += (uint32_t)used;
    while (mac->frame_cycles >= CPU_HZ / TICK_HZ)
    {
        mac->frame_cycles -= CPU_HZ / TICK_HZ;
        mac_via_set_flag(&mac->via1, VIA_INT_CA1);
        mac_rbv_vblank(&mac->rbv);
    }
    m68k_vm_set_irq(mac_rbv_irq(&mac->rbv) ? IRQ_LEVEL_RBV
                    : mac_via_irq(&mac->via1) ? IRQ_LEVEL_VIA1 : 0);
}


int mac_run(mac_t *mac, int cycles)
{
    int used = 0;

    while (used < cycles && !m68k_vm_halted() && !mac->break_hit)
    {
        int slice = cycles - used < TIME_SLICE ? cycles - used : TIME_SLICE;
        int ran = m68k_vm_run(slice);

        if (ran <= 0)
        {
            ran = 1;
        }
        used += ran;
        advance_devices(mac, ran);
    }
    return used;
}


uint32_t mac_unimpl_total(const mac_t *mac)
{
    return mac->unimpl_total;
}


/*
 * Input. The two ADB devices are at address 2 (keyboard) and 3 (mouse); the
 * handlers registered for them are called with A0 -> { count, byte 1, byte 2 }. A mouse packet is: byte 1 =
 * button (bit 7, set = up) and dy (7 bits, signed), byte 2 = dx.
 */
#define ADB_KEYBOARD 2
#define ADB_MOUSE 3
#define INPUT_QUEUE 64
#define VECTOR_LEVEL_1 0x64     /* autovector of interrupt level 1 */

static void queue_input(mac_t *mac, uint8_t device, uint8_t b1, uint8_t b2)
{
    if (mac->input_count >= INPUT_QUEUE)
    {
        return;
    }
    uint8_t *slot = mac->input[(mac->input_head + mac->input_count) % INPUT_QUEUE];

    slot[0] = device;
    slot[1] = b1;
    slot[2] = b2;
    mac->input_count++;
}


void mac_mouse_move(mac_t *mac, int dx, int dy, bool button)
{
    bool first = true;

    /* A packet carries at most 63 pixels each way. */
    while (first || dx != 0 || dy != 0)
    {
        int sx = dx > 63 ? 63 : dx < -63 ? -63 : dx;
        int sy = dy > 63 ? 63 : dy < -63 ? -63 : dy;

        queue_input(mac, ADB_MOUSE, (uint8_t)((button ? 0 : 0x80) | (sy & 0x7F)),
                    (uint8_t)(0x80 | (sx & 0x7F)));
        dx -= sx;
        dy -= sy;
        first = false;
    }
}


void mac_key(mac_t *mac, uint8_t adb_code, bool down)
{
    /* Second byte 0xFF = no second key. */
    queue_input(mac, ADB_KEYBOARD, (uint8_t)((down ? 0 : 0x80) | (adb_code & 0x7F)),
                0xFF);
}


/*
 * The ADB Manager keeps one 12-byte entry per device from the address in
 * low memory ADBBase ($CF8); +2 is the device's address, +4 its handler, +8
 * its data area (the table the ROM's own lookup routine at 0xA3BA walks).
 */
#define LM_ADBBASE 0xCF8
#define ADB_ENTRY_SIZE 12
#define ADB_ENTRIES 17

/* Next queued packet for the inject stub (see stub.s). */
static void emul_input_pop(mac_t *mac)
{
    uint32_t packet = MAC_STUB_BASE + MAC_STUB_PACKET;
    uint32_t entry = mac_bus_read(mac, LM_ADBBASE, 4);
    uint8_t *slot = mac->input[mac->input_head];
    uint8_t device;

    m68k_vm_set_reg(M68K_VM_REG_D0 + 1, 0);
    if (mac->input_count == 0)
    {
        mac->injecting = false;
        return;
    }
    device = slot[0];
    for (int i = 0; i < ADB_ENTRIES; i++, entry += ADB_ENTRY_SIZE)
    {
        if (mac_bus_read(mac, entry + 2, 1) == device)
        {
            mac_bus_write(mac, packet, 1, 2);
            mac_bus_write(mac, packet + 1, 1, slot[1]);
            mac_bus_write(mac, packet + 2, 1, slot[2]);
            m68k_vm_set_reg(M68K_VM_REG_A0, packet);
            m68k_vm_set_reg(M68K_VM_REG_A0 + 1, mac_bus_read(mac, entry + 4, 4));
            m68k_vm_set_reg(M68K_VM_REG_A0 + 2, mac_bus_read(mac, entry + 8, 4));
            m68k_vm_set_reg(M68K_VM_REG_D0, ((uint32_t)device << 4) | 0xC);
            m68k_vm_set_reg(M68K_VM_REG_D0 + 1, 1);
            break;
        }
    }
    mac->input_head = (mac->input_head + 1) % INPUT_QUEUE;
    mac->input_count--;
}


/*
 * Take an "interrupt" at the instruction at 'pc': push a format 0 exception
 * frame (status register, PC, format/vector word) on the supervisor stack and
 * continue in the inject stub with interrupt level 1, as the ADB interrupt
 * would have. Only done when the guest would accept a level 1 interrupt.
 */
static void start_injection(mac_t *mac, uint32_t pc)
{
    uint32_t sr = m68k_vm_get_reg(M68K_VM_REG_SR);
    uint32_t sp = m68k_vm_get_reg(M68K_VM_REG_A0 + 7) - 8;

    mac_bus_write(mac, sp, 2, sr);
    mac_bus_write(mac, sp + 2, 4, pc);
    mac_bus_write(mac, sp + 6, 2, VECTOR_LEVEL_1);
    m68k_vm_set_reg(M68K_VM_REG_A0 + 7, sp);
    m68k_vm_set_reg(M68K_VM_REG_SR,
                    (sr & ~SR_INT_MASK) | 0x0100 | SR_SUPERVISOR);
    m68k_vm_set_reg(M68K_VM_REG_PC, MAC_STUB_BASE + MAC_STUB_INJECT);
    mac->injecting = true;
}
