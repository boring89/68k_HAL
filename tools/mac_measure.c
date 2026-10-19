/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * tools/mac_run -H: HiDPI measurements (docs/hidpi.md). Everything here watches the Mac IIci ROM
 * (368CADFE) from outside, before every instruction:
 *
 * - The A-line trap dispatcher starts at ROM 0x408099B0 (the line-A exception vector). The trap word is
 *   at the stacked PC. A Toolbox trap ($A800-$AFFF) leaves the dispatcher with RTS at 0x408099D6 (or RTD
 *   #4 at 0x408099EC when the auto-pop bit $0400 is set), an OS trap ($A000-$A7FF) with JSR at
 *   0x40809A04 or 0x40809A22: the next instruction is the trap's routine. Found by disassembling the
 *   ROM (mac_run -D 408099B0,70).
 * - A trap is "running" from its dispatch until the PC reaches its return address: the instruction after
 *   the trap word, or for an auto-pop trap the return address on the stack above the exception frame
 *   (the glue's caller). A frame is also dropped when the stack pointer is above where it was (the
 *   trap left some other way).
 * - Writes to the NuBus card's video RAM (emu/mac vram_hook) are put in one of four groups: inside a
 *   QuickDraw or Color QuickDraw trap (tools/mac_traps.h) at interrupt level 0; inside another trap; at an
 *   interrupt level above 0 (the cursor is drawn by the vertical blanking interrupt); outside every
 *   trap. Each group also counts 32x32-pixel tiles touched per frame (the unit a shadow screen would
 *   track).
 * - The addresses of the QuickDraw routines in the ROM are read from the Toolbox trap table ($0E00,
 *   4 bytes per trap) at the first Toolbox trap. Entering one of them with a JSR, JMP, BSR, RTS or RTD
 *   that is not the dispatcher's means a call that a trap patch would not see: from ROM code (inside
 *   the ROM) or from RAM code (a patch calling the original, or a program).
 * - At DrawChar, DrawString, DrawText and StdText the current port (thePort: the long at the address in
 *   (A5)) gives txFont (offset 68), txFace (70) and txSize (74) (Inside Macintosh: Imaging With
 *   QuickDraw, GrafPort / CGrafPort).
 */

#include "mac_measure.h"
#include "mac_traps.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "emu/m68k/vm.h"

#define DISPATCH_ENTRY 0x408099B0u
#define TOOLBOX_RTS 0x408099D6u
#define TOOLBOX_RTD 0x408099ECu
#define OS_JSR_A 0x40809A04u
#define OS_JSR_B 0x40809A22u
#define ROM_START 0x40800000u
#define ROM_END 0x40880000u
#define TOOLBOX_TABLE 0x0E00u

#define TRAP_KEYS 4096      /* key = normalized trap word - $A000 */
#define STACK_MAX 64
#define MAP_SIZE 8192       /* open addressing: routine address -> key */
#define TILE 32
#define TILE_WORDS 512      /* 16384 tiles: 3840x2160 has 120 x 68 */
#define FONT_MAX 64
#define WRITER_MAX 512

enum
{
    CAT_QD,
    CAT_TRAP,
    CAT_IRQ,
    CAT_NONE,
    CAT_COUNT
};

static const char *const cat_names[CAT_COUNT] = {
    "inside a QuickDraw trap", "inside another trap", "interrupt level > 0", "outside every trap"};

typedef struct
{
    uint16_t key;
    uint32_t ret;
    uint32_t sp;
} frame_t;

typedef struct
{
    uint32_t addr;
    uint16_t key;
    bool used, ambiguous;
} entry_t;

typedef struct
{
    uint32_t pc;
    uint64_t bytes;
    uint32_t first_frame, last_frame;
    uint8_t cat;
} writer_t;

typedef struct
{
    uint16_t font, size;
    uint8_t face;
    uint32_t count;
} font_use_t;

static struct
{
    bool on;
    uint32_t frames;
    uint32_t prev_pc;
    uint16_t pending;           /* key of a trap between dispatch and its routine (0 = none) */
    bool table_read;
    frame_t stack[STACK_MAX];
    uint32_t depth, dropped;

    uint64_t dispatches[TRAP_KEYS];
    uint64_t direct_rom[TRAP_KEYS];
    uint64_t direct_ram[TRAP_KEYS];
    uint64_t qd_bytes[TRAP_KEYS];
    uint64_t trap_bytes[TRAP_KEYS];
    entry_t map[MAP_SIZE];

    uint64_t writes[CAT_COUNT], bytes[CAT_COUNT];
    uint64_t none_rom_bytes, none_ram_bytes;
    uint32_t tiles_now[CAT_COUNT][TILE_WORDS];
    uint64_t tile_frames[CAT_COUNT];
    uint32_t tile_max[CAT_COUNT];
    uint32_t frames_with[CAT_COUNT];

    font_use_t fonts[FONT_MAX];
    uint32_t font_count;
    writer_t writers[WRITER_MAX];   /* who wrote outside QuickDraw: by instruction address */
    uint32_t writer_count, writers_lost;
} m;

static uint16_t key_of(uint32_t op)
{
    /* Toolbox: trap number in bits 0-9 (bit 10 = auto-pop); OS: bits 0-7 (bits 8-10 = modifiers). */
    if (op >= 0xA800u)
        return (uint16_t)(0x800u | (op & 0x3FFu));
    return (uint16_t)(op & 0xFFu);
}

static const mac_trap_info_t *trap_info(uint16_t key)
{
    size_t lo = 0, hi = sizeof mac_traps / sizeof mac_traps[0];

    while (lo < hi)
    {
        size_t mid = (lo + hi) / 2;

        if (mac_traps[mid].key == key)
            return &mac_traps[mid];
        if (mac_traps[mid].key < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

/* QuickDraw and Color QuickDraw traps (by Multiversal Interfaces' grouping, tools/mac_traps.h). */
static bool is_quickdraw(uint16_t key)
{
    const mac_trap_info_t *info = trap_info(key);

    return info != NULL && info->quickdraw;
}

static bool in_rom(uint32_t pc)
{
    return pc >= ROM_START && pc < ROM_END;
}

static entry_t *map_slot(uint32_t addr)
{
    uint32_t h = (addr * 2654435761u) % MAP_SIZE;

    while (m.map[h].used && m.map[h].addr != addr)
        h = (h + 1) % MAP_SIZE;
    return &m.map[h];
}

static void map_add(uint32_t addr, uint16_t key)
{
    entry_t *e = map_slot(addr);

    if (!e->used)
    {
        e->used = true;
        e->addr = addr;
        e->key = key;
    }
    else if (e->key != key)
    {
        e->ambiguous = true; /* several traps share it (e.g. "unimplemented") */
    }
}

static void read_rom_table(mac_t *mac)
{
    for (uint32_t i = 0; i < 1024; i++)
    {
        uint16_t key = (uint16_t)(0x800u | i);

        if (!is_quickdraw(key))
            continue;

        uint32_t addr = mac_bus_read(mac, TOOLBOX_TABLE + 4 * i, 4);

        if (in_rom(addr))
            map_add(addr, key);
    }
    m.table_read = true;
}

static void note_font(mac_t *mac)
{
    uint32_t a5 = m68k_vm_get_reg(M68K_VM_REG_A0 + 5);
    uint32_t port = mac_bus_read(mac, mac_bus_read(mac, a5, 4), 4);

    if (port == 0 || port >= 0x40000000u)
        return;

    uint16_t font = (uint16_t)mac_bus_read(mac, port + 68, 2);
    uint8_t face = (uint8_t)mac_bus_read(mac, port + 70, 1);
    uint16_t size = (uint16_t)mac_bus_read(mac, port + 74, 2);

    for (uint32_t i = 0; i < m.font_count; i++)
    {
        if (m.fonts[i].font == font && m.fonts[i].size == size && m.fonts[i].face == face)
        {
            m.fonts[i].count++;
            return;
        }
    }
    if (m.font_count < FONT_MAX)
        m.fonts[m.font_count++] = (font_use_t){font, size, face, 1};
}

static void vram_hook(void *ctx, uint32_t card, uint32_t offset, uint32_t size)
{
    mac_t *mac = ctx;
    uint32_t sr = m68k_vm_get_reg(M68K_VM_REG_SR);
    int cat = CAT_NONE;
    int qd = -1;

    if (card != 0)
        return;

    for (int i = (int)m.depth - 1; i >= 0 && qd < 0; i--)
    {
        if (is_quickdraw(m.stack[i].key))
            qd = i;
    }
    if (((sr >> 8) & 7) != 0)
        cat = CAT_IRQ;
    else if (qd >= 0)
        cat = CAT_QD;
    else if (m.depth > 0)
        cat = CAT_TRAP;

    m.writes[cat]++;
    m.bytes[cat] += size;
    if (cat == CAT_QD)
        m.qd_bytes[m.stack[qd].key] += size;
    else if (cat == CAT_TRAP)
        m.trap_bytes[m.stack[m.depth - 1].key] += size;
    else if (cat == CAT_NONE)
        *(in_rom(m.prev_pc) ? &m.none_rom_bytes : &m.none_ram_bytes) += size;

    if (cat == CAT_NONE || cat == CAT_IRQ)
    {
        /* The instruction doing the write is the one the hook saw last. */
        uint32_t i = 0;

        while (i < m.writer_count && (m.writers[i].pc != m.prev_pc || m.writers[i].cat != cat))
            i++;
        if (i == m.writer_count && m.writer_count < WRITER_MAX)
            m.writers[m.writer_count++] = (writer_t){m.prev_pc, 0, m.frames, m.frames, (uint8_t)cat};
        if (i < m.writer_count)
        {
            m.writers[i].bytes += size;
            m.writers[i].last_frame = m.frames;
        }
        else
        {
            m.writers_lost++;
        }
    }

    uint32_t bpp;
    uint32_t rb = mac_card_row_bytes(mac, card, &bpp);

    if (rb == 0)
        return;

    uint32_t per_row = (rb * 8 / bpp + TILE - 1) / TILE;
    uint32_t y = offset / rb;
    uint32_t x0 = (offset % rb) * 8 / bpp;
    uint32_t x1 = ((offset + size - 1) % rb) * 8 / bpp;

    for (uint32_t x = x0 / TILE; x <= x1 / TILE; x++)
    {
        uint32_t tile = (y / TILE) * per_row + x;

        if (tile < TILE_WORDS * 32)
            m.tiles_now[cat][tile >> 5] |= 1u << (tile & 31);
    }
}

void measure_start(mac_t *mac)
{
    memset(&m, 0, sizeof m);
    m.on = true;
    mac->vram_hook = vram_hook;
    mac->vram_hook_ctx = mac;
}

void measure_insn(mac_t *mac, uint32_t pc)
{
    if (!m.on)
        return;

    uint32_t sp = m68k_vm_get_reg(M68K_VM_REG_A0 + 7);
    uint32_t prev = m.prev_pc;

    m.prev_pc = pc;

    /* Traps that have returned (or whose stack is gone). */
    while (m.depth > 0 && (pc == m.stack[m.depth - 1].ret || sp > m.stack[m.depth - 1].sp + 16))
        m.depth--;

    bool from_dispatcher = prev == TOOLBOX_RTS || prev == TOOLBOX_RTD || prev == OS_JSR_A || prev == OS_JSR_B;

    if (from_dispatcher && m.pending != 0)
    {
        if (is_quickdraw(m.pending))
            map_add(pc, m.pending);
        m.pending = 0;
    }
    else
    {
        entry_t *e = map_slot(pc);

        if (e->used && !e->ambiguous && m.table_read)
        {
            uint16_t opcode = (uint16_t)mac_bus_read(mac, prev, 2);
            bool call = (opcode & 0xFF80u) == 0x4E80u ||   /* JSR, JMP */
                        (opcode & 0xFF00u) == 0x6100u ||   /* BSR */
                        opcode == 0x4E75u || opcode == 0x4E74u;  /* RTS, RTD */

            if (call)
                (in_rom(prev) ? m.direct_rom : m.direct_ram)[e->key]++;
        }
    }

    if (pc != DISPATCH_ENTRY)
        return;

    uint32_t stacked_pc = mac_bus_read(mac, sp + 2, 4);
    uint32_t op = mac_bus_read(mac, stacked_pc, 2);

    if ((op & 0xF000u) != 0xA000u)
        return;

    uint16_t key = key_of(op);

    if (op >= 0xA800u && !m.table_read)
        read_rom_table(mac);

    m.dispatches[key]++;
    m.pending = key;
    if (op == 0xA882u || op == 0xA883u || op == 0xA884u || op == 0xA885u)
        note_font(mac);

    if (m.depth == STACK_MAX)
    {
        memmove(m.stack, m.stack + 1, sizeof m.stack[0] * (STACK_MAX - 1));
        m.depth--;
        m.dropped++;
    }
    m.stack[m.depth].key = key;
    m.stack[m.depth].ret = op >= 0xA800u && (op & 0x0400u) ? mac_bus_read(mac, sp + 8, 4) : stacked_pc + 2;
    m.stack[m.depth].sp = sp;
    m.depth++;
}

void measure_frame(mac_t *mac)
{
    (void)mac;
    if (!m.on)
        return;

    m.frames++;
    for (int c = 0; c < CAT_COUNT; c++)
    {
        uint32_t n = 0;

        for (uint32_t w = 0; w < TILE_WORDS; w++)
            n += (uint32_t)__builtin_popcount(m.tiles_now[c][w]);
        m.tile_frames[c] += n;
        if (n > m.tile_max[c])
            m.tile_max[c] = n;
        if (n > 0)
            m.frames_with[c]++;
        memset(m.tiles_now[c], 0, sizeof m.tiles_now[c]);
    }
}

static const char *trap_name(uint16_t key)
{
    const mac_trap_info_t *info = trap_info(key);

    return info != NULL ? info->name : "";
}

static int compare_desc(const void *a, const void *b, const uint64_t *values)
{
    uint64_t va = values[*(const uint16_t *)a], vb = values[*(const uint16_t *)b];

    return va < vb ? 1 : va > vb ? -1 : 0;
}

static const uint64_t *sort_values;

static int by_value(const void *a, const void *b)
{
    return compare_desc(a, b, sort_values);
}

/* Keys sorted by values, largest first; returns how many are non-zero. */
static uint32_t ranked(const uint64_t *values, uint16_t *keys)
{
    uint32_t n = 0;

    for (uint32_t k = 0; k < TRAP_KEYS; k++)
    {
        if (values[k] != 0)
            keys[n++] = (uint16_t)k;
    }
    sort_values = values;
    qsort(keys, n, sizeof keys[0], by_value);
    return n;
}

static uint32_t trap_word(uint16_t key)
{
    return 0xA000u + key;
}

static int by_bytes(const void *a, const void *b)
{
    const writer_t *wa = a, *wb = b;

    return wa->bytes < wb->bytes ? 1 : wa->bytes > wb->bytes ? -1 : 0;
}

/* The QuickDraw routine (ROM entry from the trap table) at or below pc: where the write happened. */
static const entry_t *routine_below(uint32_t pc)
{
    const entry_t *best = NULL;

    for (uint32_t i = 0; i < MAP_SIZE; i++)
    {
        const entry_t *e = &m.map[i];

        if (e->used && !e->ambiguous && e->addr <= pc && in_rom(e->addr) && (best == NULL || e->addr > best->addr))
            best = e;
    }
    return best;
}

static int by_font(const void *a, const void *b)
{
    const font_use_t *fa = a, *fb = b;

    return fa->count < fb->count ? 1 : fa->count > fb->count ? -1 : 0;
}

void measure_report(mac_t *mac, FILE *out)
{
    static uint16_t keys[TRAP_KEYS];
    uint64_t total = 0, qd_total = 0, written = 0;

    for (uint32_t k = 0; k < TRAP_KEYS; k++)
    {
        total += m.dispatches[k];
        if (is_quickdraw((uint16_t)k))
            qd_total += m.dispatches[k];
    }
    for (int c = 0; c < CAT_COUNT; c++)
        written += m.bytes[c];

    fprintf(out, "frames %u, instructions %u, trap dispatches %llu (QuickDraw %llu), stale frames dropped %u\n\n",
            m.frames, mac->insn_count, (unsigned long long)total, (unsigned long long)qd_total, m.dropped);

    fprintf(out, "== traps by calls (top 40)\n");
    uint32_t n = ranked(m.dispatches, keys);

    for (uint32_t i = 0; i < n && i < 40; i++)
        fprintf(out, "  %04X %-16s %10llu\n", trap_word(keys[i]), trap_name(keys[i]),
                (unsigned long long)m.dispatches[keys[i]]);

    fprintf(out, "\n== video RAM writes (card in slot $C), %llu bytes\n", (unsigned long long)written);
    fprintf(out, "  %-24s %10s %12s %7s %14s %10s %12s\n", "group", "writes", "bytes", "share", "tiles/frame",
            "max tiles", "frames with");
    for (int c = 0; c < CAT_COUNT; c++)
        fprintf(out, "  %-24s %10llu %12llu %6.2f%% %14.2f %10u %12u\n", cat_names[c],
                (unsigned long long)m.writes[c], (unsigned long long)m.bytes[c],
                written ? 100.0 * (double)m.bytes[c] / (double)written : 0.0,
                m.frames ? (double)m.tile_frames[c] / m.frames : 0.0, m.tile_max[c], m.frames_with[c]);
    fprintf(out, "  outside every trap: %llu bytes from ROM code, %llu from RAM code\n",
            (unsigned long long)m.none_rom_bytes, (unsigned long long)m.none_ram_bytes);

    fprintf(out, "\n== instructions that wrote the video RAM outside QuickDraw (top 20; nearest QuickDraw entry below)\n");
    qsort(m.writers, m.writer_count, sizeof m.writers[0], by_bytes);
    for (uint32_t i = 0; i < m.writer_count && i < 20; i++)
    {
        const writer_t *w = &m.writers[i];
        const entry_t *e = in_rom(w->pc) ? routine_below(w->pc) : NULL;

        fprintf(out, "  %08X %-22s %10llu bytes  frames %u-%u", w->pc, cat_names[w->cat],
                (unsigned long long)w->bytes, w->first_frame, w->last_frame);
        if (e != NULL)
            fprintf(out, "  %s+%X", trap_name(e->key), w->pc - e->addr);
        fprintf(out, "\n");
    }
    if (m.writers_lost > 0)
        fprintf(out, "  (%u writes from more places not listed)\n", m.writers_lost);

    fprintf(out, "\n== QuickDraw traps that wrote the video RAM (innermost QuickDraw trap)\n");
    n = ranked(m.qd_bytes, keys);
    for (uint32_t i = 0; i < n && i < 20; i++)
        fprintf(out, "  %04X %-16s %12llu bytes\n", trap_word(keys[i]), trap_name(keys[i]),
                (unsigned long long)m.qd_bytes[keys[i]]);

    fprintf(out, "\n== other traps that wrote the video RAM (innermost trap)\n");
    n = ranked(m.trap_bytes, keys);
    for (uint32_t i = 0; i < n && i < 20; i++)
        fprintf(out, "  %04X %-16s %12llu bytes\n", trap_word(keys[i]), trap_name(keys[i]),
                (unsigned long long)m.trap_bytes[keys[i]]);

    fprintf(out, "\n== QuickDraw routines entered without the dispatcher (ROM addresses from the trap table)\n");
    fprintf(out, "  %-4s %-16s %12s %14s %14s\n", "trap", "", "dispatched", "from ROM code", "from RAM code");
    static uint64_t direct[TRAP_KEYS];
    uint64_t rom_sum = 0, ram_sum = 0;

    for (uint32_t k = 0; k < TRAP_KEYS; k++)
    {
        direct[k] = m.direct_rom[k] + m.direct_ram[k];
        rom_sum += m.direct_rom[k];
        ram_sum += m.direct_ram[k];
    }
    n = ranked(direct, keys);
    for (uint32_t i = 0; i < n && i < 80; i++)
        fprintf(out, "  %04X %-16s %12llu %14llu %14llu\n", trap_word(keys[i]), trap_name(keys[i]),
                (unsigned long long)m.dispatches[keys[i]], (unsigned long long)m.direct_rom[keys[i]],
                (unsigned long long)m.direct_ram[keys[i]]);
    fprintf(out, "  total: %llu from ROM code, %llu from RAM code (QuickDraw dispatches %llu)\n",
            (unsigned long long)rom_sum, (unsigned long long)ram_sum, (unsigned long long)qd_total);

    fprintf(out, "\n== fonts at DrawChar, DrawString, DrawText, StdText (txFont, txSize 0 = default, txFace bits)\n");
    qsort(m.fonts, m.font_count, sizeof m.fonts[0], by_font);
    for (uint32_t i = 0; i < m.font_count; i++)
        fprintf(out, "  font %5u size %3u face %02X  %8u calls\n", m.fonts[i].font, m.fonts[i].size, m.fonts[i].face,
                m.fonts[i].count);
}
