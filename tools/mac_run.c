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
 *   -E   attach another disk image as drive 9, 10, 11 (repeatable; mounted once the Finder runs)
 *   -Z   MB: the Mac's RAM (default 8)
 *   -P   frame: with -F, press the keyboard's power key at that frame (System 7 asks whether to shut down)
 *   -Y   with -F, a restart of the Mac OS does not end the run (Shut Down and Restart end it otherwise)
 *   -x   save guest memory to a file: -x <addr hex> <length hex> <file>
 *   -F   run this many whole frames (416000 cycles each) and print the screen's CRC-32
 *   -M   dx,dy,button,frame: with -F, move the mouse by (dx,dy) at that frame (repeatable)
 *   -K   code,down,frame: with -F, a key (ADB code) goes down/up at that frame (repeatable)
 *   -R   present the disk as write-protected
 *   -H   file: with -F, measure for a 2x HiDPI screen (traps, video RAM writes outside QuickDraw, direct
 *        calls into QuickDraw, fonts; tools/mac_measure.c, docs/hidpi.md) and write the report there
 *   -A   folder: show this host folder to the Mac as a read-only volume "HAL" (the external file
 *        system, emu/mac/fsm.c, as the kernel shows its own files; mounted once the Finder runs)
 *   -e   image[@sector]: show the eHFS volume of this image file (at that sector) to the Mac as a
 *        volume it reads and writes (kernel/mac/fs_ehfs.c; after -A's volume when both are given)
 *   -X   file: the parameter RAM (256 bytes): read from the file at the start if it exists, written at the end
 *        (like 68k_hal/pram on a 68k_HAL disk, kernel/mac/pram.h)
 *   -p   write the screen as a PPM image at the end
 *   -m   print 16 bytes of guest memory at this address (hex) at the end
 *   -s   after a -b break, run this many more instructions
 *   -a   list all remembered unimplemented accesses (default: the first 24)
 *   -u   collapse loops in the trace tail (hide addresses seen recently)
 *   -r   print the parameter RAM at the end
 *   -c   list the ROM blocks (64 bytes) in the order they were first executed
 *   -b   stop when the PC reaches this address, or range lo-hi (hex); also with -F
 *   -T   opcode: log each A-line trap (opcode 0 = all) with D0/A0/A1 (the Mac OS trap dispatcher entry)
 *   -D   addr,count: disassemble count instructions from addr (hex) and exit
 *   -G   with -F, list the QuickDraw GDevices (DeviceList) at the end
 *   -C   WxH[xD]: the NuBus video card (slot $C) starts in this resolution (default 640x480) and depth
 *        (D = 1, 2, 4, 8, 16 or 32 bits per pixel; default 1)
 *   -2   WxH: a second NuBus video card (slot $D) in this resolution
 *   -q   with -2, write the second card's screen as a PPM image at the end
 *   -I   no NuBus video card: the built-in video (640x480, 8 bits at most) is the screen
 *   -V   addr: print every change of the 32-bit guest value at that address (hex), with the instruction that did it
 *   -L   @addr: print the registers each time the PC is addr (hex)
 *   -L   lo-hi: print every instruction with the registers while the instruction count (decimal) is in that range
 *   -W   lo-hi,frame: with -F, from that frame on stop after the instruction that writes guest RAM in that
 *        range (hex); the instruction is the last one of the trace tail (-t)
 *   -f   print the first N executed instructions (when the run is short)
 *   -t   print the last N executed instructions at the end (default 32)
 *
 * The ROM is read from the file and never copied anywhere. Prints the CPU
 * registers, the trace tail and the unimplemented hardware accesses, which is
 * what to look at when the ROM gets stuck.
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "emu/m68k/vm.h"
#include "emu/mac/fs_paths.h"
#include "emu/mac/mac.h"
#include "kernel/mac/fs_ehfs.h"
#include "mac_measure.h"
#include "third_party/musashi/m68k.h"

#define RAM_SIZE (8u * 1024 * 1024)
#define VRAM_BASE 0xFBB08000u      /* from the ROM's UniversalInfo video table */
#define VRAM_SIZE (512u * 1024)
#define INPUT_SCRIPT_MAX 256        /* -M, -J and -K entries kept */

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


/* -E path: more disks (drives 9..11), kept in memory like the first one. */
typedef struct
{
    uint8_t *data;
    uint32_t size;
} extra_disk_t;

static extra_disk_t extra_disks[MAC_MAX_DISKS - 1];


static int extra_read(void *ctx, uint32_t offset, void *buf, uint32_t len)
{
    const extra_disk_t *d = ctx;

    if ((uint64_t)offset + len > d->size)
    {
        return -1;
    }
    memcpy(buf, d->data + offset, len);
    return 0;
}


static int extra_write(void *ctx, uint32_t offset, const void *buf, uint32_t len)
{
    extra_disk_t *d = ctx;

    if ((uint64_t)offset + len > d->size)
    {
        return -1;
    }
    memcpy(d->data + offset, buf, len);
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

/* The whole picture, kept up to date with the rows that changed (mac_render_changes). */
static uint32_t *screen;
static uint32_t screen_w, screen_h;

static const uint32_t *render_screen(void)
{
    uint32_t w, h;

    mac_screen_size(&mac, &w, &h);
    if (w != screen_w || h != screen_h)
    {
        free(screen);
        screen = calloc((size_t)w * h, 4);
        screen_w = w;
        screen_h = h;
        mac.card[0].all_dirty = true;
    }
    if (screen == NULL || mac_render_changes(&mac, screen, NULL) < 0)
        return NULL;
    return screen;
}

static void write_ppm(const char *path, const uint32_t *pixels)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL)
        return;
    fprintf(f, "P6\n%u %u\n255\n", screen_w, screen_h);
    for (uint32_t i = 0; i < screen_w * screen_h; i++)
    {
        fputc(pixels[i] >> 16, f);
        fputc(pixels[i] >> 8, f);
        fputc(pixels[i], f);
    }
    fclose(f);
}
static uint32_t g_tail = 32;
static uint32_t log_lo, log_hi, log_pc;
static bool want_log;
static const char *measure_path;    /* -H */
static const char *host_folder;     /* -A */
static mac_fs_paths_t host_volume;
static const char *ehfs_image;      /* -e */
static int ehfs_fd = -1;
static ehfs_t ehfs_volume;
static mac_fs_ehfs_t ehfs_mac;

/* -e: the image file as a device of 512-byte sectors */
static int image_read(block_device_t *d, uint64_t lba, uint32_t count, void *buffer)
{
    (void)d;
    return pread(ehfs_fd, buffer, (size_t)count * 512, (off_t)(lba * 512)) == (ssize_t)count * 512 ? 0 : -1;
}

static int image_write(block_device_t *d, uint64_t lba, uint32_t count, const void *buffer)
{
    (void)d;
    return pwrite(ehfs_fd, buffer, (size_t)count * 512, (off_t)(lba * 512)) == (ssize_t)count * 512 ? 0 : -1;
}

static const block_ops_t image_ops = {image_read, image_write};
static block_device_t image_device = {"image", 512, 0, &image_ops, NULL};

static uint64_t ehfs_clock(void)
{
    return (uint64_t)(0xD0000000u - 2082844800u) * 1000000000u;     /* the Mac's fixed date (rtc_seconds) */
}

/* Open and mount the -e volume; 0, or -1 after saying why. */
static int ehfs_attach(mac_t *mac)
{
    char path[1024];
    uint64_t sector = 0;
    const char *at = strrchr(ehfs_image, '@');

    snprintf(path, sizeof path, "%.*s", at != NULL ? (int)(at - ehfs_image) : (int)strlen(ehfs_image), ehfs_image);
    if (at != NULL)
        sector = strtoull(at + 1, NULL, 0);
    ehfs_fd = open(path, O_RDWR);
    if (ehfs_fd < 0)
    {
        printf("mac: cannot open the eHFS image %s\n", path);
        return -1;
    }
    image_device.block_count = (uint64_t)lseek(ehfs_fd, 0, SEEK_END) / 512;

    int r = ehfs_mount(&ehfs_volume, &image_device, sector, false);

    if (r != EHFS_OK)
    {
        printf("mac: the eHFS volume of %s cannot be mounted (%d)\n", path, r);
        return -1;
    }
    mac_fs_ehfs_init(&ehfs_mac, &ehfs_volume, ehfs_clock);
    mac_fsm_attach(mac, &ehfs_mac.ops);
    return 0;
}

/* -A: the host folder for emu/mac/fs_paths.c */
static void host_path(char *out, size_t size, const char *path)
{
    snprintf(out, size, "%s%s%s", host_folder, path[0] != '\0' ? "/" : "", path);
}

static int host_entry(void *ctx, const char *path, uint32_t index, char *name, uint32_t size)
{
    char full[1024];
    struct dirent *e;
    uint32_t i = 0;
    int result = -1;

    (void)ctx;
    host_path(full, sizeof full, path);

    DIR *d = opendir(full);

    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL)
    {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (i++ == index)
        {
            snprintf(name, size, "%s", e->d_name);
            result = 0;
            break;
        }
    }
    closedir(d);
    return result;
}

static int host_stat(void *ctx, const char *path, bool *is_dir, uint32_t *size, uint32_t *mtime)
{
    char full[1024];
    struct stat st;

    (void)ctx;
    host_path(full, sizeof full, path);
    if (stat(full, &st) != 0)
        return -1;
    *is_dir = S_ISDIR(st.st_mode);
    *size = st.st_size > 0xFFFFFFFF ? 0xFFFFFFFFu : (uint32_t)st.st_size;
    *mtime = (uint32_t)st.st_mtime;
    return 0;
}

static int32_t host_read(void *ctx, const char *path, uint32_t offset, void *buffer, uint32_t length)
{
    char full[1024];

    (void)ctx;
    host_path(full, sizeof full, path);

    int fd = open(full, O_RDONLY);

    if (fd < 0)
        return -1;

    ssize_t n = pread(fd, buffer, length, offset);

    close(fd);
    return n < 0 ? -1 : (int32_t)n;
}

static const mac_fs_host_t host_ops = {NULL, host_entry, host_stat, host_read};
static bool show_gd;
static int card_res;            /* -C: index in mac_card_resolutions (0 = 640x480) */
static bool builtin_only;       /* -I */
static uint32_t card_depth;     /* -C: 0-5 = 1-32 bits */
static int second_res = -1;     /* -2: the second card's resolution */
static const char *second_ppm;  /* -q */
static uint32_t dis_addr, dis_n;
static bool trap_log;
static uint32_t trap_filter;
static uint32_t val_addr, val_last, val_prev_pc;
static bool val_on;

/* -L: one line per instruction with the registers (the hook runs before the instruction). */
static void log_hook(void *ctx, uint32_t pc)
{
    (void)ctx;
    if (measure_path != NULL)
    {
        measure_insn(&mac, pc);
    }
    if (val_on)
    {
        uint32_t v = mac_bus_read(&mac, val_addr, 4);

        if (v != val_last)
        {
            printf("%u V %08X: %08X -> %08X by the instruction at %08X (next pc %08X)\n", mac.insn_count, val_addr,
                   val_last, v, val_prev_pc, pc);
            val_last = v;
        }
        val_prev_pc = pc;
    }
    if (trap_log && pc == 0x408099B0)
    {
        uint32_t sp = m68k_vm_get_reg(M68K_VM_REG_A0 + 7);
        uint32_t op = mac_bus_read(&mac, mac_bus_read(&mac, sp + 2, 4), 2);

        if (trap_filter == 0 || trap_filter == op)
        {
            printf("%u T %04X from %08X D0=%08X A0=%08X A1=%08X\n", mac.insn_count, op, mac_bus_read(&mac, sp + 2, 4),
                   m68k_vm_get_reg(M68K_VM_REG_D0), m68k_vm_get_reg(M68K_VM_REG_A0),
                   m68k_vm_get_reg(M68K_VM_REG_A0 + 1));
            if (op == 0xA06E) /* Slot Manager: also the SpBlock (A0 = parameter block pointer in A0? no: A0 holds the SpBlock) */
            {
                uint32_t a = m68k_vm_get_reg(M68K_VM_REG_A0);

                printf("    SpBlock@%08X result=%08X ptr=%08X cat=%04X ctype=%04X sw=%04X hw=%04X slot=%02X id=%02X\n", a,
                       mac_bus_read(&mac, a, 4), mac_bus_read(&mac, a + 4, 4), mac_bus_read(&mac, a + 0x28, 2),
                       mac_bus_read(&mac, a + 0x2A, 2), mac_bus_read(&mac, a + 0x2C, 2),
                       mac_bus_read(&mac, a + 0x2E, 2), mac_bus_read(&mac, a + 0x31, 1), mac_bus_read(&mac, a + 0x32, 1));
            }
        }
    }
    if (log_pc == 0 && log_hi == 0)
    {
        return;
    }
    if (log_pc != 0 && pc != log_pc)
    {
        return;
    }
    if (mac.insn_count < log_lo || mac.insn_count > log_hi)
    {
        return;
    }
    char text[128];

    m68k_disassemble(text, pc, M68K_CPU_TYPE_68030);
    printf("%u %08X: %-32s D0=%08X D1=%08X D2=%08X D3=%08X D4=%08X D5=%08X D6=%08X A0=%08X A1=%08X A2=%08X A3=%08X A6=%08X A7=%08X\n",
           mac.insn_count, pc, text, m68k_vm_get_reg(M68K_VM_REG_D0), m68k_vm_get_reg(M68K_VM_REG_D0 + 1),
           m68k_vm_get_reg(M68K_VM_REG_D0 + 2), m68k_vm_get_reg(M68K_VM_REG_D0 + 3),
           m68k_vm_get_reg(M68K_VM_REG_D0 + 4), m68k_vm_get_reg(M68K_VM_REG_D0 + 5),
           m68k_vm_get_reg(M68K_VM_REG_D0 + 6), m68k_vm_get_reg(M68K_VM_REG_A0),
           m68k_vm_get_reg(M68K_VM_REG_A0 + 1), m68k_vm_get_reg(M68K_VM_REG_A0 + 2),
           m68k_vm_get_reg(M68K_VM_REG_A0 + 3), m68k_vm_get_reg(M68K_VM_REG_A0 + 6), m68k_vm_get_reg(M68K_VM_REG_A0 + 7));
    if (log_pc != 0) /* -L @addr: also the first 48 bytes at A1 and the return address */
    {
        uint32_t a1 = m68k_vm_get_reg(M68K_VM_REG_A0 + 1);
        uint32_t sp = m68k_vm_get_reg(M68K_VM_REG_A0 + 7);

        printf("    ret=%08X A1:", mac_bus_read(&mac, sp, 4));
        for (uint32_t i = 0; i < 48; i += 4)
        {
            printf(" %08X", mac_bus_read(&mac, a1 + i, 4));
        }
        printf("\n    A0:");
        a1 = m68k_vm_get_reg(M68K_VM_REG_A0);
        for (uint32_t i = 0; i < 56; i += 4)
        {
            printf(" %08X", mac_bus_read(&mac, a1 + i, 4));
        }
        printf("\n");
    }
}

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
    struct { int x, y, b; uint32_t frame; bool jump; } mouse[INPUT_SCRIPT_MAX];
    int nmouse = 0;
    struct { int code, down; uint32_t frame; } keys[INPUT_SCRIPT_MAX];
    int nkeys = 0;
    const char *ppm_path = NULL;
    const char *wav_path = NULL;
    const char *disk_out_path = NULL;
    const char *xpram_path = NULL;
    int64_t beep_frame = -1;
    const char *save_path = NULL;
    uint32_t save_addr = 0;
    uint32_t save_len = 0;
    const char *disk_path = NULL;
    const char *extra_paths[MAC_MAX_DISKS - 1];
    int nextra = 0;
    uint32_t ram_size = RAM_SIZE;
    uint32_t power_frame = 0;
    bool keep_restarting = false;
    bool disk_read_only = false;
    uint32_t dump[8];
    int ndump = 0;
    uint32_t extra = 0;
    uint32_t break_hi = 0;
    uint32_t watch_lo = 0, watch_hi = 0, watch_frame = 0;
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
        else if (argv[i][0] == '-' && argv[i][1] == 'P' && i + 1 < argc)
        {
            power_frame = strtoul(argv[++i], NULL, 0); /* -P frame: press the power key then */
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'Y')
        {
            keep_restarting = true; /* -Y: a restart of the Mac OS does not end the run */
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'Z' && i + 1 < argc)
        {
            ram_size = (uint32_t)strtoul(argv[++i], NULL, 0) * 1024u * 1024u; /* -Z MB: the Mac's RAM */
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'E' && i + 1 < argc && nextra < MAC_MAX_DISKS - 1)
        {
            extra_paths[nextra++] = argv[++i];
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
            if (nmouse < INPUT_SCRIPT_MAX)
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
            if (nmouse < INPUT_SCRIPT_MAX)
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
            if (nkeys < INPUT_SCRIPT_MAX)
            {
                sscanf(argv[++i], "%i,%d,%u", &keys[nkeys].code,
                       &keys[nkeys].down, &keys[nkeys].frame);
                nkeys++;
            }
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'L' && i + 1 < argc)
        {
            if (argv[i + 1][0] == '@') /* -L @addr: log only when the PC is addr (hex) */
            {
                sscanf(argv[++i] + 1, "%x", &log_pc);
                log_hi = 0xFFFFFFFFu;
            }
            else
            {
                sscanf(argv[++i], "%u-%u", &log_lo, &log_hi);
            }
            want_log = true;
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'W' && i + 1 < argc && strchr(argv[i + 1], ','))
        {
            sscanf(argv[++i], "%x-%x,%u", &watch_lo, &watch_hi, &watch_frame);
            if (watch_hi == 0)
            {
                watch_hi = watch_lo;
            }
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'V' && i + 1 < argc)
        {
            sscanf(argv[++i], "%x", &val_addr); /* -V addr: print each change of the 32-bit value at addr (hex) */
            val_on = true;
            want_log = true;
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'T' && i + 1 < argc)
        {
            sscanf(argv[++i], "%x", &trap_filter); /* -T opcode: log every A-line trap (0 = all) with that opcode (hex) */
            trap_log = true;
            want_log = true;
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'D' && i + 1 < argc)
        {
            sscanf(argv[++i], "%x,%u", &dis_addr, &dis_n);
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'C' && i + 1 < argc)
        {
            unsigned int w = 0, h = 0, bits = 1;

            sscanf(argv[++i], "%ux%ux%u", &w, &h, &bits);
            card_depth = 0;
            while (card_depth < 5 && (1u << card_depth) < bits)
                card_depth++;
            card_res = mac_card_find_resolution(w, h);
            if (card_res < 0)
            {
                fprintf(stderr, "-C: the card has no %ux%u mode\n", w, h);
                return 1;
            }
        }
        else if (argv[i][0] == '-' && argv[i][1] == '2' && i + 1 < argc)
        {
            unsigned int w = 0, h = 0;

            sscanf(argv[++i], "%ux%u", &w, &h);
            second_res = mac_card_find_resolution(w, h);
            if (second_res < 0)
            {
                fprintf(stderr, "-2: the card has no %ux%u mode\n", w, h);
                return 1;
            }
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'q' && i + 1 < argc)
        {
            second_ppm = argv[++i];
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'I')
        {
            card_res = -1;
            builtin_only = true;
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'G')
        {
            show_gd = true; /* -G: with -F, list the QuickDraw GDevices (DeviceList) at the end */
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'R')
        {
            disk_read_only = true;
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'W' && i + 1 < argc)
        {
            wav_path = argv[++i];
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'H' && i + 1 < argc)
        {
            measure_path = argv[++i];
            want_log = true;
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'A' && i + 1 < argc)
        {
            host_folder = argv[++i];
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'e' && i + 1 < argc)
        {
            ehfs_image = argv[++i];
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'X' && i + 1 < argc)
        {
            xpram_path = argv[++i];
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
    ram = calloc(1, ram_size);
    if (ram == NULL || mac_init(&mac, ram, ram_size, rom, rom_size) != 0)
    {
        fprintf(stderr, "mac_init failed (ROM size %u)\n", rom_size);
        return 1;
    }
    g_tail = tail;
    atexit(on_exit_dump);
    mac.vram = calloc(1, VRAM_SIZE);
    mac.vram_size = VRAM_SIZE;
    mac.vram_base = VRAM_BASE;
    if (!builtin_only && mac_card_attach(&mac, calloc(1, MAC_CARD_VRAM_SIZE), (uint32_t)card_res, card_depth) != 0)
    {
        fprintf(stderr, "cannot attach the video card\n");
        return 1;
    }
    if (!builtin_only && second_res >= 0 &&
        mac_card_attach(&mac, calloc(1, MAC_CARD_VRAM_SIZE), (uint32_t)second_res, 0) != 0)
    {
        fprintf(stderr, "cannot attach the second video card\n");
        return 1;
    }
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
    for (int e = 0; e < nextra; e++)
    {
        long en;
        FILE *ef = fopen(extra_paths[e], "rb");

        if (ef == NULL)
        {
            fprintf(stderr, "cannot read %s\n", extra_paths[e]);
            return 1;
        }
        fseek(ef, 0, SEEK_END);
        en = ftell(ef);
        fseek(ef, 0, SEEK_SET);
        extra_disks[e].data = malloc(en);
        extra_disks[e].size = (uint32_t)en;
        if (extra_disks[e].data == NULL || fread(extra_disks[e].data, 1, en, ef) != (size_t)en)
        {
            fprintf(stderr, "cannot read %s\n", extra_paths[e]);
            return 1;
        }
        fclose(ef);
        mac.more_disks[e].read = extra_read;
        mac.more_disks[e].write = extra_write;
        mac.more_disks[e].ctx = &extra_disks[e];
        mac.more_disks[e].blocks = extra_disks[e].size / 512;
    }
    mac.audio = audio_sink;
    mac.trace_enabled = true;
    mac.rtc_seconds = 0xD0000000u;     /* a fixed date in 2013 */
    if (want_log)
    {
        mac.pc_hook = log_hook;
    }
    if (measure_path != NULL)
    {
        measure_start(&mac);
    }
    if (host_folder != NULL)
    {
        mac_fs_paths_init(&host_volume, &host_ops, "HAL");
        mac_fsm_attach(&mac, &host_volume.ops);
    }
    if (ehfs_image != NULL && ehfs_attach(&mac) != 0)
        return 1;
    if (xpram_path != NULL)
    {
        FILE *xf = fopen(xpram_path, "rb");

        if (xf != NULL)
        {
            if (fread(mac.xpram, 1, sizeof mac.xpram, xf) != sizeof mac.xpram)
                printf("mac: %s is shorter than 256 bytes\n", xpram_path);
            fclose(xf);
        }
    }
    mac_reset(&mac);
    if (dis_n > 0)
    {
        /* -D addr,count: disassemble from that address (hex) and exit */
        for (uint32_t a = dis_addr, n = 0; n < dis_n; n++)
        {
            char text[128];
            uint32_t len = m68k_disassemble(text, a, M68K_CPU_TYPE_68030);

            printf("%08X: %s\n", a, text);
            a += len;
        }
        return 0;
    }

    if (frames > 0)
    {
        /* Exactly what the kernel does (kernel/mac/machine.c): whole frames. */
        const uint32_t *shot;

        /* -b works in frame mode too: the run stops at the first time the PC gets there. */
        mac.break_lo = break_pc;
        mac.break_hi = break_hi;

        for (uint32_t f = 0; f < frames && !mac.break_hit; f++)
        {
            if (watch_hi != 0 && f == watch_frame)
            {
                mac.watch_lo = watch_lo;
                mac.watch_hi = watch_hi;
            }
            for (int k = 0; k < nkeys; k++)
            {
                if (keys[k].frame == f)
                {
                    mac_key(&mac, (uint8_t)keys[k].code, keys[k].down != 0);
                }
            }
            if (power_frame != 0 && (f == power_frame || f == power_frame + 10))
            {
                mac_power_key(&mac, f == power_frame);
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
            measure_frame(&mac);
            if (mac.power_request == MAC_POWER_RESTART && keep_restarting)
            {
                printf("mac: restart at frame %u (continuing)\n", f);
                mac.power_request = MAC_POWER_NONE;
            }
            if (mac.power_request != MAC_POWER_NONE)
            {
                printf("mac: %s at frame %u\n", mac.power_request == MAC_POWER_OFF ? "power off" : "restart", f);
                break;
            }
        }
        for (uint32_t v = 0; v < mac.fsm.volumes; v++)
            printf("mac: volume \"%s\": install result %d, %s, drive %d\n", mac.fsm.volume[v].ops->volume_name,
                   mac.fsm.volume[v].mount_error != 0 ? mac.fsm.volume[v].mount_error : mac.fsm.install_error,
                   mac.fsm.volume[v].mounted ? "mounted" : "not mounted", mac.fsm.volume[v].drive);
        if (ehfs_image != NULL)
        {
            ehfs_unmount(&ehfs_volume);
            fsync(ehfs_fd);
            close(ehfs_fd);
        }
        if (mac.fsm.volumes > 0)
        {
            printf("mac: %u file system requests\n", mac.fsm.requests);
            if (show_xpram)     /* -r: also the last requests */
            {
                uint32_t n = mac.fsm.requests < 64 ? mac.fsm.requests : 64;

                for (uint32_t k = mac.fsm.requests - n; k < mac.fsm.requests; k++)
                    printf("mac: fsm request %u: select %04X perm %u -> %d\n", k + 1, mac.fsm.log[k % 64].select,
                           mac.fsm.log[k % 64].permission, mac.fsm.log[k % 64].result);
            }
        }
        if (measure_path != NULL)
        {
            FILE *mf = fopen(measure_path, "w");

            if (mf != NULL)
            {
                measure_report(&mac, mf);
                fclose(mf);
                printf("mac: measurements written to %s\n", measure_path);
            }
        }
        if (xpram_path != NULL)
        {
            FILE *xf = fopen(xpram_path, "wb");

            if (xf != NULL)
            {
                fwrite(mac.xpram, 1, sizeof mac.xpram, xf);
                fclose(xf);
            }
            printf("mac: parameter RAM %s\n", mac.xpram_dirty ? "changed" : "unchanged");
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
        shot = render_screen();
        if (ppm_path != NULL && shot != NULL)
        {
            write_ppm(ppm_path, shot);
        }
        if (shot != NULL)
        {
            printf("mac: screen crc %x after %u frames (%ux%u)\n",
                   crc32(shot, screen_w * screen_h), frames, screen_w, screen_h);
        }
        if (second_ppm != NULL && mac.card[1].present)
        {
            uint32_t w, h;

            mac_card_screen_size(&mac, 1, &w, &h);
            uint32_t *second = calloc((size_t)w * h, 4);

            mac.card[1].all_dirty = true;
            if (second != NULL && mac_card_render(&mac, 1, second, NULL) >= 0)
            {
                uint32_t keep_w = screen_w, keep_h = screen_h;

                screen_w = w;
                screen_h = h;
                write_ppm(second_ppm, second);
                screen_w = keep_w;
                screen_h = keep_h;
                int32_t x = 0, y = 0;
                bool placed = mac_card_position(&mac, 1, &x, &y) == 0;

                printf("mac: second screen %ux%u written to %s (%s at %d,%d)\n", w, h, second_ppm,
                       placed ? "on the desktop" : "not a screen", (int)x, (int)y);
            }
            free(second);
        }
        if (show_gd)
        {
            /* DeviceList ($8A8): handle -> GDevice, gdNextGD at +$1E, gdFlags at +$14 */
            uint32_t h = mac_bus_read(&mac, 0x8A8, 4);

            for (int n = 0; h != 0 && n < 20; n++)
            {
                uint32_t gd = mac_bus_read(&mac, h, 4);

                printf("gdevice %d: handle %08X ptr %08X flags %04X next %08X\n", n, h, gd,
                       mac_bus_read(&mac, gd + 0x14, 2), mac_bus_read(&mac, gd + 0x1E, 4));
                h = mac_bus_read(&mac, gd + 0x1E, 4);
            }
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
        const uint32_t *pixels = render_screen();

        if (pixels != NULL)
        {
            write_ppm(ppm_path, pixels);
            printf("screen written to %s (%ux%u)\n", ppm_path, screen_w, screen_h);
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
