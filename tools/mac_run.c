/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Host runner for the Macintosh IIci model (emu/mac).
 *
 * Usage: mac_run [-n instructions] [-t N] <rom-file>
 *
 *   -n   run at most this many instructions (default 1000000)
 *   -d   attach this disk image (read into memory; never written back)
 *   -x   save guest memory to a file: -x <addr hex> <length hex> <file>
 *   -F   run this many whole frames (416000 cycles each) and print the screen's CRC-32
 *   -M   dx,dy,button,frame: with -F, move the mouse by (dx,dy) at that frame (repeatable)
 *   -K   code,down,frame: with -F, a key (ADB code) goes down/up at that frame (repeatable)
 *   -R   present the disk as write-protected
 *   -p   write the screen as a PPM image at the end
 *   -m   print 16 bytes of guest memory at this address (hex) at the end
 *   -s   after a -b break, run this many more instructions
 *   -a   list all remembered unimplemented accesses (default: the first 24)
 *   -u   collapse loops in the trace tail (hide addresses seen recently)
 *   -r   print the parameter RAM at the end
 *   -c   list the ROM blocks (64 bytes) in the order they were first executed
 *   -b   stop when the PC reaches this address, or range lo-hi (hex)
 *   -f   print the first N executed instructions (when the run is short)
 *   -t   print the last N executed instructions at the end (default 32)
 *
 * The ROM is read from the file and never copied anywhere. Prints the CPU
 * registers, the trace tail and the unimplemented hardware accesses, which is
 * what to look at when the ROM gets stuck.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu/m68k/vm.h"
#include "emu/mac/mac.h"
#include "third_party/musashi/m68k.h"

#define RAM_SIZE (8u * 1024 * 1024)
#define VRAM_BASE 0xFBB08000u      /* from the ROM's UniversalInfo video table */
#define VRAM_SIZE (512u * 1024)

void m68k_vm_fatal(const char *message)
{
    fprintf(stderr, "fatal: %s\n", message);
    exit(2);
}


/* Callbacks of the Musashi disassembler (see third_party/musashi/m68k.h). */
unsigned int m68k_read_disassembler_16(unsigned int address)
{
    return m68k_vm_read16(address);
}


unsigned int m68k_read_disassembler_32(unsigned int address)
{
    return m68k_vm_read32(address);
}


static uint8_t *read_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *buf;

    if (f == NULL)
    {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(n);
    if (buf == NULL || fread(buf, 1, n, f) != (size_t)n)
    {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *size = (uint32_t)n;
    return buf;
}


/* Same CRC-32 as kernel/mac/machine.c, over the 32-bit pixels. */
static uint32_t crc32(const uint32_t *pixels, uint32_t count)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < count; i++)
    {
        for (int b = 0; b < 4; b++)
        {
            crc ^= (pixels[i] >> (8 * b)) & 0xFF;
            for (int k = 0; k < 8; k++)
            {
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
            }
        }
    }
    return ~crc;
}


static uint8_t *disk_image;
static uint32_t disk_size;


static int disk_read(void *ctx, uint32_t offset, void *buf, uint32_t len)
{
    (void)ctx;
    if ((uint64_t)offset + len > disk_size)
    {
        return -1;
    }
    memcpy(buf, disk_image + offset, len);
    return 0;
}


static int disk_write(void *ctx, uint32_t offset, const void *buf, uint32_t len)
{
    (void)ctx;
    if ((uint64_t)offset + len > disk_size)
    {
        return -1;
    }
    memcpy(disk_image + offset, buf, len);
    return 0;
}


static void print_regs(void)
{
    for (int i = 0; i < 8; i++)
    {
        printf("D%d=%08X A%d=%08X%s", i, m68k_vm_get_reg(M68K_VM_REG_D0 + i),
               i, m68k_vm_get_reg(M68K_VM_REG_A0 + i), i % 2 ? "\n" : "  ");
    }
    printf("PC=%08X SR=%04X\n", m68k_vm_get_reg(M68K_VM_REG_PC),
           m68k_vm_get_reg(M68K_VM_REG_SR));
}


static mac_t mac;
static uint32_t g_tail = 32;

/* Sound: -W records what the sound chip plays (8-bit mono) and writes a WAV file at the end. */
static uint8_t *audio_data;
static size_t audio_length, audio_capacity;
static uint32_t audio_rate;

static void audio_sink(void *ctx, const uint8_t *samples, uint32_t count, uint32_t rate)
{
    (void)ctx;
    if (audio_length + count > audio_capacity)
    {
        audio_capacity = (audio_length + count) * 2;
        audio_data = realloc(audio_data, audio_capacity);
    }
    memcpy(audio_data + audio_length, samples, count);
    audio_length += count;
    audio_rate = rate;
}

static void write_wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint32_t rate = audio_rate ? audio_rate : 22254;
    uint8_t header[44] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\1\0\1\0";

    if (f == NULL)
    {
        return;
    }
    uint32_t riff = (uint32_t)(36 + audio_length), bytes_per_second = rate, data = (uint32_t)audio_length;

    memcpy(header + 4, &riff, 4);
    memcpy(header + 24, &rate, 4);
    memcpy(header + 28, &bytes_per_second, 4);
    header[32] = 1;     /* block align */
    header[34] = 8;     /* bits per sample */
    memcpy(header + 36, "data", 4);
    memcpy(header + 40, &data, 4);
    fwrite(header, 1, sizeof header, f);
    fwrite(audio_data, 1, audio_length, f);
    fclose(f);
}

/* Musashi calls exit() on errors it cannot handle: show where we were. */
static void on_exit_dump(void)
{
    if (mac.insn_count == 0)
    {
        return;
    }
    printf("exit after %u instructions, PC=%08X\n", mac.insn_count,
           m68k_vm_get_reg(M68K_VM_REG_PC));
    for (uint32_t i = mac.pmmu_total > 16 ? mac.pmmu_total - 16 : 0;
         i < mac.pmmu_total; i++)
    {
        char text[128];

        m68k_disassemble(text, mac.pmmu_pc[i % 16], M68K_CPU_TYPE_68030);
        printf("PMMU %08X: %s\n", mac.pmmu_pc[i % 16], text);
    }
    for (uint32_t n = g_tail; n > 0; n--)
    {
        char text[128];
        uint32_t idx;

        if (n > mac.trace_next || n > MAC_TRACE_SIZE)
        {
            continue;
        }
        idx = (mac.trace_next - n) % MAC_TRACE_SIZE;
        m68k_disassemble(text, mac.trace[idx].pc, M68K_CPU_TYPE_68030);
        printf("%08X: %s\n", mac.trace[idx].pc, text);
    }
}


int main(int argc, char **argv)
{
    uint32_t max_insns = 1000000;
    uint32_t tail = 32;
    uint32_t head = 0;
    uint32_t break_pc = 0;
    uint32_t frames = 0;
    struct { int x, y, b; uint32_t frame; bool jump; } mouse[32];
    int nmouse = 0;
    struct { int code, down; uint32_t frame; } keys[32];
    int nkeys = 0;
    const char *ppm_path = NULL;
    const char *wav_path = NULL;
    const char *disk_out_path = NULL;
    int64_t beep_frame = -1;
    const char *save_path = NULL;
    uint32_t save_addr = 0;
    uint32_t save_len = 0;
    const char *disk_path = NULL;
    bool disk_read_only = false;
    uint32_t dump[8];
    int ndump = 0;
    uint32_t extra = 0;
    uint32_t break_hi = 0;
    bool flow = false;
    bool show_xpram = false;
    uint32_t unimpl_max = 24;
    bool collapse = false;
    uint32_t recent[48] = { 0 };
    uint32_t skipped = 0;
    const char *rom_path = NULL;
    uint32_t rom_size = 0;
    uint8_t *rom;
    uint8_t *ram;

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] == 'n' && i + 1 < argc)
        {
            max_insns = strtoul(argv[++i], NULL, 0);
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'm' && i + 1 < argc && ndump < 8)
        {
            dump[ndump++] = strtoul(argv[++i], NULL, 16);
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'd' && i + 1 < argc)
        {
            disk_path = argv[++i];
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'x' && i + 3 < argc)
        {
            save_addr = strtoul(argv[++i], NULL, 16);
            save_len = strtoul(argv[++i], NULL, 16);
            save_path = argv[++i];
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'F' && i + 1 < argc)
        {
            frames = strtoul(argv[++i], NULL, 0);
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'M' && i + 1 < argc)
        {
            /* -M x,y,button,frame: move the mouse at that frame (with -F) */
            if (nmouse < 32)
            {
                sscanf(argv[++i], "%d,%d,%d,%u", &mouse[nmouse].x,
                       &mouse[nmouse].y, &mouse[nmouse].b,
                       &mouse[nmouse].frame);
                mouse[nmouse].jump = false;
                nmouse++;
            }
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'J' && i + 1 < argc)
        {
            /* -J x,y,button,frame: put the pointer at screen position (x, y) at that frame */
            if (nmouse < 32)
            {
                sscanf(argv[++i], "%d,%d,%d,%u", &mouse[nmouse].x,
                       &mouse[nmouse].y, &mouse[nmouse].b,
                       &mouse[nmouse].frame);
                mouse[nmouse].jump = true;
                nmouse++;
            }
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'K' && i + 1 < argc)
        {
            /* -K adbcode,down,frame: a key goes down (1) or up (0) at that frame */
            if (nkeys < 32)
            {
                sscanf(argv[++i], "%i,%d,%u", &keys[nkeys].code,
                       &keys[nkeys].down, &keys[nkeys].frame);
                nkeys++;
            }
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'R')
        {
            disk_read_only = true;
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'W' && i + 1 < argc)
        {
            wav_path = argv[++i];
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'O' && i + 1 < argc)
        {
            /* -O file: write the disk image (with what the Mac changed) to that file at the end */
            disk_out_path = argv[++i];
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'B' && i + 1 < argc)
        {
            beep_frame = strtol(argv[++i], NULL, 0);
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'p' && i + 1 < argc)
        {
            ppm_path = argv[++i];
        }
        else if (argv[i][0] == '-' && argv[i][1] == 's' && i + 1 < argc)
        {
            extra = strtoul(argv[++i], NULL, 0);
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'a')
        {
            unimpl_max = MAC_UNIMPL_LOG_SIZE;
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'u')
        {
            collapse = true;
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'r')
        {
            show_xpram = true;
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'c')
        {
            flow = true;
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'b' && i + 1 < argc)
        {
            char *end;

            break_pc = strtoul(argv[++i], &end, 16);
            break_hi = *end == '-' ? strtoul(end + 1, NULL, 16) : break_pc;
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'f' && i + 1 < argc)
        {
            head = strtoul(argv[++i], NULL, 0);
        }
        else if (argv[i][0] == '-' && argv[i][1] == 't' && i + 1 < argc)
        {
            tail = strtoul(argv[++i], NULL, 0);
        }
        else
        {
            rom_path = argv[i];
        }
    }
    if (rom_path == NULL)
    {
        fprintf(stderr, "usage: mac_run [-n insns] [-t N] <rom-file>\n");
        return 1;
    }

    rom = read_file(rom_path, &rom_size);
    if (rom == NULL)
    {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 1;
    }
    if (mac_rom_patch(rom, rom_size) != 0)
    {
        fprintf(stderr, "unsupported ROM (need Mac IIci 368CADFE)\n");
        return 1;
    }
    ram = calloc(1, RAM_SIZE);
    if (ram == NULL || mac_init(&mac, ram, RAM_SIZE, rom, rom_size) != 0)
    {
        fprintf(stderr, "mac_init failed (ROM size %u)\n", rom_size);
        return 1;
    }
    g_tail = tail;
    atexit(on_exit_dump);
    mac.vram = calloc(1, VRAM_SIZE);
    mac.vram_size = VRAM_SIZE;
    mac.vram_base = VRAM_BASE;
    if (disk_path != NULL)
    {
        FILE *df = fopen(disk_path, "rb");
        long dn;

        if (df == NULL)
        {
            fprintf(stderr, "cannot read %s\n", disk_path);
            return 1;
        }
        fseek(df, 0, SEEK_END);
        dn = ftell(df);
        fseek(df, 0, SEEK_SET);
        disk_image = malloc(dn);
        if (disk_image == NULL || fread(disk_image, 1, dn, df) != (size_t)dn)
        {
            fprintf(stderr, "cannot read %s\n", disk_path);
            return 1;
        }
        fclose(df);
        disk_size = (uint32_t)dn;
        /* The image is only ever changed in memory, never written back. */
        mac.disk.read = disk_read;
        mac.disk.write = disk_write;
        mac.disk.blocks = disk_size / 512;
        mac.disk.read_only = disk_read_only;
    }
    mac.audio = audio_sink;
    mac.trace_enabled = true;
    mac.rtc_seconds = 0xD0000000u;     /* a fixed date in 2013 */
    mac_reset(&mac);

    if (frames > 0)
    {
        /* Exactly what the kernel does (kernel/mac/machine.c): whole frames. */
        static uint32_t shot[MAC_SCREEN_W * MAC_SCREEN_H];

        for (uint32_t f = 0; f < frames; f++)
        {
            for (int k = 0; k < nkeys; k++)
            {
                if (keys[k].frame == f)
                {
                    mac_key(&mac, (uint8_t)keys[k].code, keys[k].down != 0);
                }
            }
            for (int m = 0; m < nmouse; m++)
            {
                if (mouse[m].frame == f)
                {
                    if (mouse[m].jump)
                    {
                        mac_mouse_warp(&mac, mouse[m].x, mouse[m].y);
                        mac_mouse_move(&mac, 0, 0, mouse[m].b != 0);
                    }
                    else
                    {
                        mac_mouse_move(&mac, mouse[m].x, mouse[m].y, mouse[m].b != 0);
                    }
                }
            }
            if (beep_frame >= 0 && f == (uint32_t)beep_frame)
            {
                mac_sysbeep(&mac);
            }
            mac_run(&mac, 416000);
        }
        if (disk_out_path != NULL && disk_image != NULL)
        {
            FILE *df = fopen(disk_out_path, "wb");

            if (df != NULL)
            {
                fwrite(disk_image, 1, disk_size, df);
                fclose(df);
            }
        }
        if (wav_path != NULL)
        {
            write_wav(wav_path);
            printf("mac: %zu audio samples\n", audio_length);
        }
        if (ppm_path != NULL && mac_render(&mac, shot) == 0)
        {
            FILE *pf = fopen(ppm_path, "wb");

            if (pf != NULL)
            {
                fprintf(pf, "P6\n%d %d\n255\n", MAC_SCREEN_W, MAC_SCREEN_H);
                for (uint32_t i = 0; i < MAC_SCREEN_W * MAC_SCREEN_H; i++)
                {
                    fputc(shot[i] >> 16, pf);
                    fputc(shot[i] >> 8, pf);
                    fputc(shot[i], pf);
                }
                fclose(pf);
            }
        }
        if (mac_render(&mac, shot) == 0)
        {
            printf("mac: screen crc %x after %u frames\n",
                   crc32(shot, MAC_SCREEN_W * MAC_SCREEN_H), frames);
        }
        max_insns = 0;
    }
    mac.break_lo = break_pc;
    mac.break_hi = break_hi;
    while (mac.insn_count < max_insns && !m68k_vm_halted() && !mac.break_hit)
    {
        mac_run(&mac, 1000);
    }
    if (mac.break_hit)
    {
        printf("break at %08X\n", m68k_vm_get_reg(M68K_VM_REG_PC));
        mac.break_hi = 0;
        for (uint32_t k = 0; k < extra; k++)
        {
            mac_run(&mac, 1);
        }
    }

    printf("instructions: %u%s\n", mac.insn_count,
           m68k_vm_halted() ? " (halted)" : "");
    print_regs();

    if (head > 0 && mac.trace_next <= MAC_TRACE_SIZE)
    {
        printf("-- first %u instructions --\n", head);
        for (uint32_t i = 0; i < head && i < mac.trace_next; i++)
        {
            char text[128];

            m68k_disassemble(text, mac.trace[i].pc, M68K_CPU_TYPE_68030);
            printf("%08X: %s\n", mac.trace[i].pc, text);
        }
        tail = 0;
    }
    printf("-- last %u instructions --\n", tail);
    for (uint32_t n = tail, shown = 0; n > 0; n--)
    {
        uint32_t idx;
        uint32_t pc;
        bool repeat = false;
        char text[128];

        if (n > mac.trace_next || n > MAC_TRACE_SIZE)
        {
            continue;
        }
        idx = (mac.trace_next - n) % MAC_TRACE_SIZE;
        pc = mac.trace[idx].pc;

        /* Loops: do not print an address seen in the last 48 lines again. */
        for (uint32_t k = 1; k <= 48 && k < shown + 1 && collapse; k++)
        {
            repeat = repeat || recent[(shown - k) % 48] == pc;
        }
        if (repeat)
        {
            skipped++;
            continue;
        }
        if (skipped > 0)
        {
            printf("   ... %u repeated\n", skipped);
            skipped = 0;
        }
        recent[shown % 48] = pc;
        shown++;
        m68k_disassemble(text, pc, M68K_CPU_TYPE_68030);
        printf("%08X: %s\n", pc, text);
    }
    if (skipped > 0)
    {
        printf("   ... %u repeated\n", skipped);
    }

    if (save_path != NULL)
    {
        FILE *sf = fopen(save_path, "wb");

        for (uint32_t i = 0; sf != NULL && i < save_len; i++)
        {
            fputc(m68k_vm_read8(save_addr + i), sf);
        }
        if (sf != NULL)
        {
            fclose(sf);
        }
    }
    if (ppm_path != NULL)
    {
        static uint32_t pixels[MAC_SCREEN_W * MAC_SCREEN_H];
        FILE *f = fopen(ppm_path, "wb");

        if (f != NULL && mac_render(&mac, pixels) == 0)
        {
            fprintf(f, "P6\n%d %d\n255\n", MAC_SCREEN_W, MAC_SCREEN_H);
            for (uint32_t i = 0; i < MAC_SCREEN_W * MAC_SCREEN_H; i++)
            {
                fputc(pixels[i] >> 16, f);
                fputc(pixels[i] >> 8, f);
                fputc(pixels[i], f);
            }
            printf("screen written to %s (depth %u bpp)\n", ppm_path,
                   1u << (mac.rbv.depth & 3));
        }
        if (f != NULL)
        {
            fclose(f);
        }
    }
    for (int k = 0; k < ndump; k++)
    {
        printf("mem %08X:", dump[k]);
        for (uint32_t b = 0; b < 16; b += 2)
        {
            printf(" %04X", m68k_vm_read16(dump[k] + b));
        }
        printf("\n");
    }
    if (show_xpram)
    {
        printf("VIA1 ifr=%02X ier=%02X acr=%02X pcr=%02X t1=%04X t2=%04X ora=%02X ddra=%02X orb=%02X ddrb=%02X\n",
               mac.via1.ifr, mac.via1.ier, mac.via1.acr, mac.via1.pcr,
               mac.via1.t1_counter, mac.via1.t2_counter, mac.via1.ora,
               mac.via1.ddra, mac.via1.orb, mac.via1.ddrb);
        printf("RBV latched=%d sier=%02X rier=%02X other:", mac.rbv.vblank_latched, mac.rbv.sier, mac.rbv.rier);
        for (int i = 0; i < 0x20; i++)
        {
            printf(" %02X", mac.rbv.other[i]);
        }
        printf("\n");
        printf("clock chip commands (first %u):", mac.clk_log_len);
        for (uint32_t i = 0; i < mac.clk_log_len; i++)
        {
            printf(" %c%04X=%02X", (mac.clk_log[i] >> 31) ? 'R' : 'W',
                   (mac.clk_log[i] >> 8) & 0xFFFF, mac.clk_log[i] & 0xFF);
        }
        printf("\n");
        for (int row = 0; row < 16; row++)
        {
            printf("xpram %02X:", row * 16);
            for (int col = 0; col < 16; col++)
            {
                printf(" %02X", mac.xpram[row * 16 + col]);
            }
            printf("\n");
        }
    }
    if (flow)
    {
        printf("-- ROM blocks in first-seen order (insn: address) --\n");
        for (uint32_t i = 0; i < mac.flow_count; i++)
        {
            printf("%u: %08X\n", mac.flow_insn[i],
                   mac.flow_block[i] << MAC_FLOW_SHIFT);
        }
    }
    printf("-- unimplemented accesses: %u --\n", mac_unimpl_total(&mac));
    for (uint32_t i = 0; i < unimpl_max && i < mac.unimpl_used; i++)
    {
        const mac_access_t *a = &mac.unimpl[i];

        printf("%s%d %08X pc=%08X val=%08X x%u\n", a->write ? "W" : "R",
               a->size, a->addr, a->pc, a->value, a->count);
    }
    return 0;
}
