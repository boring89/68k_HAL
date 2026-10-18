/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "machine.h"

#include "adbkeys.h"
#include "emu/mac/mac.h"
#include "emu/m68k/vm.h"
#include "hal/boot.h"
#include "hal/clock.h"
#include "hal/cpu.h"
#include "hal/framebuffer.h"
#include "hal/mmu.h"
#include "hal/timer.h"
#include "kernel/lib/k_time.h"
#include "kernel/block/block.h"
#include "kernel/boot/module.h"
#include "kernel/display/fb_scale.h"
#include "kernel/display/fbcon.h"
#include "kernel/sound/sound.h"
#include "kernel/input/event_buffer.h"
#include "kernel/keyboard/keycode.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/m68k/guest.h"
#include "kernel/memory/heap.h"
#include "kernel/printf/printf.h"
#include "kernel/terminal/terminal.h"

#include <stddef.h>

#define ROM_SIZE 0x80000u
#define VRAM_SIZE 0x80000u
#define VRAM_BASE 0xFBB08000u       /* where the ROM's tables put the video RAM */
#define RAM_SIZE (8u * 1024 * 1024)
#define RTC_START 0xD0000000u       /* a fixed date: seconds since 1904 */
/*
 * Frames between two redraws: every frame, so the screen follows the Mac at
 * 60 Hz. Rendering and the write-combined screen update of the changed rows
 * cost a few ms, well inside a 16.7 ms frame (docs/mac.md, section 8).
 */
#define SHOW_EVERY 1u
#define BLOCK_SIZE 512u

/*
 * The disk image is a boot module: read-only memory. Writes go to an
 * overlay of 64 KiB chunks allocated when needed, so that the Mac can write
 * (the Finder creates its Desktop files at startup) without changing the
 * image. 4 MiB of changes are kept; more fail with an I/O error.
 */
#define CHUNK_BLOCKS 128u
#define CHUNK_BYTES (CHUNK_BLOCKS * BLOCK_SIZE)
#define MAX_CHUNKS 64u

typedef struct
{
    const uint8_t *image;
    uint32_t blocks;
    uint16_t *slot;             /* per block: 0 = from the image, else 1 + index */
    uint8_t *chunk[MAX_CHUNKS];
    uint32_t used;              /* blocks in the overlay */
} overlay_t;

static uint32_t last_crc;


static int overlay_read(void *ctx, uint32_t offset, void *buf, uint32_t len)
{
    overlay_t *ov = ctx;

    if (offset % BLOCK_SIZE != 0 || len % BLOCK_SIZE != 0 ||
        (uint64_t)offset + len > (uint64_t)ov->blocks * BLOCK_SIZE)
    {
        return -1;
    }
    for (uint32_t done = 0; done < len; done += BLOCK_SIZE)
    {
        uint32_t block = (offset + done) / BLOCK_SIZE;
        uint32_t slot = ov->slot[block];

        if (slot != 0)
        {
            slot--;
            k_memcpy((uint8_t *)buf + done,
                     ov->chunk[slot / CHUNK_BLOCKS] +
                         (slot % CHUNK_BLOCKS) * BLOCK_SIZE,
                     BLOCK_SIZE);
        }
        else
        {
            k_memcpy((uint8_t *)buf + done, ov->image + offset + done,
                     BLOCK_SIZE);
        }
    }
    return 0;
}


static int overlay_write(void *ctx, uint32_t offset, const void *buf,
                         uint32_t len)
{
    overlay_t *ov = ctx;

    if (offset % BLOCK_SIZE != 0 || len % BLOCK_SIZE != 0 ||
        (uint64_t)offset + len > (uint64_t)ov->blocks * BLOCK_SIZE)
    {
        return -1;
    }
    for (uint32_t done = 0; done < len; done += BLOCK_SIZE)
    {
        uint32_t block = (offset + done) / BLOCK_SIZE;
        uint32_t slot = ov->slot[block];

        if (slot == 0)
        {
            uint32_t index = ov->used;

            if (index / CHUNK_BLOCKS >= MAX_CHUNKS)
            {
                return -1;
            }
            if (ov->chunk[index / CHUNK_BLOCKS] == NULL)
            {
                ov->chunk[index / CHUNK_BLOCKS] = kmalloc(CHUNK_BYTES);
                if (ov->chunk[index / CHUNK_BLOCKS] == NULL)
                {
                    return -1;
                }
            }
            ov->used++;
            slot = index + 1;
            ov->slot[block] = (uint16_t)slot;
        }
        slot--;
        k_memcpy(ov->chunk[slot / CHUNK_BLOCKS] +
                     (slot % CHUNK_BLOCKS) * BLOCK_SIZE,
                 (const uint8_t *)buf + done, BLOCK_SIZE);
    }
    return 0;
}


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


/* 0: the largest whole-number enlargement that fits the screen (see fb_best_scale). */
static uint32_t screen_scale = 0;

void mac_machine_set_scale(uint32_t scale)
{
    screen_scale = scale;
}

/*
 * What the Mac's picture looks like on the screen after the last show(), to draw only the rows that changed:
 * the screen is write combining memory (hal/mmu.h), where writes are cheap and reads are not, and a Mac
 * desktop changes in a small part of the picture from one update to the next.
 */
static uint32_t *shown;
static bool shown_valid;

/* Figures of the run (mac_machine_report): time in the emulation, the rendering and the screen. */
static struct
{
    uint64_t frames;
    uint64_t emulate_ns;
    uint64_t render_ns;
    uint64_t show_ns;
    uint32_t updates;
    uint64_t rows;
    uint64_t instructions;
} perf;

/* Copy the rendered screen into the framebuffer, enlarged by scale and centered; only what changed. */
static void show(const hal_framebuffer_t *fb, const uint32_t *pixels, uint32_t scale)
{
    uint64_t start = hal_timer_ns();
    uint32_t shown_w = MAC_SCREEN_W * scale;
    uint32_t shown_h = MAC_SCREEN_H * scale;
    const uint32_t *previous = shown_valid ? shown : NULL;

    if (fb->width < shown_w || fb->height < shown_h)
    {
        /* A smaller screen than the picture: show the top left part, unscaled. */
        uint32_t w = fb->width < MAC_SCREEN_W ? fb->width : MAC_SCREEN_W;
        uint32_t h = fb->height < MAC_SCREEN_H ? fb->height : MAC_SCREEN_H;

        for (uint32_t y = 0; y < h; y++)
        {
            if (previous != NULL && k_memcmp(pixels + y * MAC_SCREEN_W, previous + y * MAC_SCREEN_W, w * 4) == 0)
                continue;

            k_memcpy(fb->base + y * fb->pitch, pixels + y * MAC_SCREEN_W, w * 4);
            perf.rows++;
        }
    }
    else
    {
        uint32_t x0 = (fb->width - shown_w) / 2;
        uint32_t y0 = (fb->height - shown_h) / 2;

        perf.rows += fb_blit_scaled_rows(fb->base + y0 * fb->pitch + x0 * 4, fb->pitch, pixels, previous,
                                         MAC_SCREEN_W, MAC_SCREEN_H, scale);
    }

    if (shown != NULL)
    {
        k_memcpy(shown, pixels, MAC_SCREEN_W * MAC_SCREEN_H * 4);
        shown_valid = true;
    }

    perf.updates++;
    perf.show_ns += hal_timer_ns() - start;
}

/* A decimal number with one digit after the point, from tenths. */
static void print_tenths(const char *label, uint64_t tenths, const char *unit)
{
    kernel_printf("%s%u.%u%s", label, (unsigned int)(tenths / 10), (unsigned int)(tenths % 10), unit);
}

/*
 * What the run cost, for a report from a real machine (dmesg keeps it): how many frames per second the Mac
 * got, how long the emulation, the rendering and the screen update took per frame, and how fast the emulated
 * CPU was. A frame is 1/60 s of a 25 MHz Mac IIci; the machine keeps real time, so anything below 60 frames per
 * second means the work of a frame took longer than 16.7 ms.
 */
static void report_performance(uint64_t wall_ns)
{
    if (perf.frames == 0 || wall_ns == 0)
        return;

    uint64_t fps10 = perf.frames * 10000000000ull / wall_ns;
    uint64_t ms10_emulate = perf.emulate_ns * 10 / perf.frames / 1000000;
    uint64_t ms10_render = perf.updates != 0 ? perf.render_ns * 10 / perf.updates / 1000000 : 0;
    uint64_t ms10_show = perf.updates != 0 ? perf.show_ns * 10 / perf.updates / 1000000 : 0;
    uint64_t rows10 = perf.updates != 0 ? perf.rows * 10 / perf.updates : 0;
    uint64_t mips10 = perf.emulate_ns != 0 ? perf.instructions * 10000 / perf.emulate_ns : 0;

    kernel_printf("mac: %u frames in %u ms", (unsigned int)perf.frames, (unsigned int)(wall_ns / 1000000));
    print_tenths(": ", fps10, " frames/s");
    kernel_printf(", %u screen updates\n", perf.updates);
    print_tenths("mac: per frame: emulation ", ms10_emulate, " ms");
    print_tenths(" (", mips10, " million instructions/s)");
    kernel_printf("\n");
    print_tenths("mac: per screen update: render ", ms10_render, " ms");
    print_tenths(", screen ", ms10_show, " ms");
    print_tenths(" (", rows10, " of 480 rows drawn)");
    kernel_printf("\n");
}


/*
 * Pass keyboard and mouse events on to the Mac. The grave accent key (`) stops
 * the Mac: it is the one key the Mac side does not need. Returns true to stop.
 */
static bool handle_input(mac_t *mac)
{
    input_event_t event;

    while (input_event_pop(&event) == 0)
    {
        if (event.type == INPUT_EVENT_KEYBOARD)
        {
            int code;

            if (event.data.keyboard.pressed &&
                event.data.keyboard.key == KEY_GRAVE)
            {
                return true;
            }
            code = mac_adb_key_code(event.data.keyboard.key);

            /*
             * The Mac's Caps Lock key latches: it reports "down" while it is on and
             * "up" while it is off. A PC key sends press and release every time, so
             * the press flips the state and the release is dropped.
             */
            if (event.data.keyboard.key == KEY_CAPS_LOCK)
            {
                static bool caps_on;

                if (event.data.keyboard.pressed)
                {
                    caps_on = !caps_on;
                    mac_key(mac, (uint8_t)code, caps_on);
                }
                continue;
            }

            if (code >= 0)
            {
                mac_key(mac, (uint8_t)code, event.data.keyboard.pressed);
            }
        }
        else if (event.type == INPUT_EVENT_MOUSE)
        {
            mac_mouse_move(mac, event.data.mouse.dx, event.data.mouse.dy,
                           (event.data.mouse.buttons & 1) != 0);
        }
    }
    return false;
}


/*
 * A disk on a block device (the ATA drive): reads and writes go straight to
 * it, so what the Mac changes is still there after a reboot. The Mac works in
 * 512-byte blocks like the drive.
 */
static int drive_read(void *ctx, uint32_t offset, void *buf, uint32_t len)
{
    if (offset % BLOCK_SIZE != 0 || len % BLOCK_SIZE != 0)
        return -1;

    return block_read(ctx, offset / BLOCK_SIZE, len / BLOCK_SIZE, buf);
}

static int drive_write(void *ctx, uint32_t offset, const void *buf, uint32_t len)
{
    if (offset % BLOCK_SIZE != 0 || len % BLOCK_SIZE != 0)
        return -1;

    return block_write(ctx, offset / BLOCK_SIZE, len / BLOCK_SIZE, buf);
}


/*
 * Sound. The model plays unsigned 8-bit mono samples (a few at a time, as the
 * emulated time passes); they are collected for one frame, converted to the
 * 48 kHz stereo the sound card takes and queued. If the card cannot take them
 * (its queue is full) they are dropped: the emulation is not slowed for sound.
 */
#define AUDIO_MAX 2048          /* source samples collected per frame (370 are normal) */
#define AUDIO_FRAMES 4800       /* output frames per frame: room for 11 kHz sources too */

static uint8_t audio_samples[AUDIO_MAX];
static uint32_t audio_count;
static uint32_t audio_rate;
static sound_resampler_t audio_resampler;

static void audio_collect(void *ctx, const uint8_t *samples, uint32_t count, uint32_t rate)
{
    (void)ctx;

    if (count > AUDIO_MAX - audio_count)
        count = AUDIO_MAX - audio_count;

    k_memcpy(audio_samples + audio_count, samples, count);
    audio_count += count;
    audio_rate = rate;
}

static void audio_flush(int16_t *out)
{
    if (audio_count == 0)
        return;

    uint32_t frames = sound_resample_u8(&audio_resampler, audio_samples, audio_count, audio_rate,
                                        out, AUDIO_FRAMES, 100);

    audio_count = 0;
    sound_write(out, frames);
}

/* Seconds from 1 Jan 1904 (the Mac epoch) to 1 Jan 1970 (the Unix epoch). */
#define MAC_EPOCH_OFFSET 2082844800u

/* The Mac's clock for an interactive run: the real time (UTC), or 0 if the machine has no clock. */
static uint32_t current_mac_time(void)
{
    hal_datetime_t now;

    if (hal_clock_now(&now) != 0)
        return 0;

    int64_t unix_time = k_epoch_from_civil(now.year, now.month, now.day, now.hour, now.minute, now.second);

    return (uint32_t)(unix_time + MAC_EPOCH_OFFSET);
}

/* One frame is MAC_FRAME_CYCLES of a 25 MHz CPU; this is how long that takes in real time. */
#define FRAME_NS ((uint64_t)MAC_FRAME_CYCLES * 1000000000u / MAC_CPU_HZ)

/*
 * Real-time pacing: the Mac's time should pass like ours, or its clock and
 * every sound would run too fast. Wait until frame number 'frame' (counted from
 * the start) is due. A host too slow to keep up just runs flat out; after a long
 * stall (e.g. a long disk access) the schedule starts over instead of racing to
 * catch up.
 */
static void pace(uint64_t *start, uint32_t frame)
{
    uint64_t due = *start + (uint64_t)frame * FRAME_NS;
    uint64_t now = hal_timer_ns();

    if (now > due + 100000000u)
    {
        *start = now - (uint64_t)frame * FRAME_NS;

        return;
    }

    while (now < due)
    {
        /* hlt wakes at the next timer tick (10 ms): only sleep if that cannot overshoot. */
        if (due - now > 12000000u)
            hal_cpu_idle();

        now = hal_timer_ns();
    }
}

#ifdef KERNEL_MAC_TEST_FOLDER
/*
 * make test-mac-persist: a scripted visit to the Finder's menus. File > New
 * Folder at frame 1500, Special > Shut Down at 2000 (which makes the system
 * write its changes to the disk). The positions were found with
 * tools/mac_run -J and screenshots of the open menus (640x480 screen).
 */
static const struct
{
    uint32_t frame;
    int x;
    int y;
    bool down;
} test_script[] = {
    {1500, 52, 9, true},    /* press on "File" */
    {1520, 67, 27, true},   /* drag to "New Folder" */
    {1540, 67, 27, false},  /* release: choose it */
    {2000, 209, 9, true},   /* press on "Special" */
    {2020, 250, 139, true}, /* drag to "Shut Down" */
    {2040, 250, 139, false},
};
#endif

/*
 * Give the screen back to the console: the mode it had before the Mac (if the Mac asked for another one),
 * then everything the console kept meanwhile, drawn again.
 */
static void release_screen(const hal_framebuffer_t *original)
{
    hal_framebuffer_t now;

    if (hal_framebuffer_get(&now) == 0 && (now.width != original->width || now.height != original->height))
        hal_framebuffer_set_mode(original->width, original->height);

    if (hal_framebuffer_get(&now) != 0 || fbcon_display() == NULL)
        return;

    /* Another size than the console was made for (the mode could not be restored): start it again, empty. */
    if (fbcon_resume(&now) != 0 && fbcon_init(&now, 0) == 0)
        terminal_init();
}

int mac_machine_run(uint32_t frames)
{
    hal_boot_module_t rom_module;
    hal_boot_module_t disk_module;
    hal_framebuffer_t fb;
    hal_framebuffer_t original;
    bool screen_taken = false;
    bool have_screen;
    uint32_t scale = screen_scale;
    mac_t *mac = kmalloc(sizeof *mac);
    uint8_t *rom = kmalloc(ROM_SIZE);
    uint8_t *vram = kmalloc(VRAM_SIZE);
    uint32_t *pixels = kmalloc(MAC_SCREEN_W * MAC_SCREEN_H * 4);
    overlay_t *ov = kmalloc(sizeof *ov);
    have_screen = hal_framebuffer_get(&fb) == 0;
    original = fb;
    shown = kmalloc(MAC_SCREEN_W * MAC_SCREEN_H * 4);
    shown_valid = false;
    k_memset(&perf, 0, sizeof perf);

    /* Enlarged: ask the display adapter for a mode that fits the picture exactly. */
    if (scale > 1 && hal_framebuffer_set_mode(MAC_SCREEN_W * scale, MAC_SCREEN_H * scale) == 0)
        have_screen = hal_framebuffer_get(&fb) == 0;

    if (have_screen && (scale == 0 || MAC_SCREEN_W * scale > fb.width || MAC_SCREEN_H * scale > fb.height))
        scale = fb_best_scale(MAC_SCREEN_W, MAC_SCREEN_H, fb.width, fb.height);

    if (scale == 0)
        scale = 1;

    int16_t *audio_out = sound_get() != NULL ? kmalloc(AUDIO_FRAMES * 4) : NULL;
    int status = -1;

    if (mac == NULL || rom == NULL || vram == NULL || pixels == NULL ||
        ov == NULL)
    {
        kernel_printf("%s\n", "mac: out of memory");
        goto out;
    }
    /* The disk: the boot module mac_disk (changes stay in memory), or else the ATA drive (changes are written). */
    bool disk_in_memory = boot_module_find(MAC_MODULE_DISK, &disk_module) != NULL;
    block_device_t *drive = disk_in_memory ? NULL : block_device_find("ata0");

    if (boot_module_find(MAC_MODULE_ROM, &rom_module) == NULL || (!disk_in_memory && drive == NULL))
    {
        kernel_printf("%s\n", "mac: needs the boot module mac_rom and a disk (module mac_disk or an ATA drive)");
        goto out;
    }
    if (rom_module.length != ROM_SIZE)
    {
        kernel_printf("%s\n", "mac: mac_rom must be 512 KiB");
        goto out;
    }

    /* The ROM is patched in a copy: the module is read-only. */
    k_memcpy(rom, hal_phys_to_virt(rom_module.base), ROM_SIZE);
    if (mac_rom_patch(rom, ROM_SIZE) != 0)
    {
        kernel_printf("%s\n", "mac: mac_rom is not a Mac IIci ROM (368CADFE)");
        goto out;
    }

    k_memset(ov, 0, sizeof *ov);

    if (disk_in_memory)
    {
        ov->image = (const uint8_t *)hal_phys_to_virt(disk_module.base);
        ov->blocks = (uint32_t)(disk_module.length / BLOCK_SIZE);
        ov->slot = kmalloc((size_t)ov->blocks * sizeof(uint16_t));
        if (ov->slot == NULL)
        {
            kernel_printf("%s\n", "mac: out of memory");
            goto out;
        }
        k_memset(ov->slot, 0, (size_t)ov->blocks * sizeof(uint16_t));
    }

    if (m68k_guest_set_size(RAM_SIZE) != 0 || m68k_guest_init() != 0)
    {
        kernel_printf("%s\n", "mac: cannot allocate the Mac's RAM");
        goto out;
    }
    if (mac_init(mac, (uint8_t *)M68K_GUEST_RAM_BASE, RAM_SIZE, rom,
                 ROM_SIZE) != 0)
    {
        kernel_printf("%s\n", "mac: cannot start the machine");
        goto out;
    }
    k_memset(vram, 0, VRAM_SIZE);
    mac->vram = vram;
    mac->vram_size = VRAM_SIZE;
    mac->vram_base = VRAM_BASE;
    mac->rtc_seconds = RTC_START;

    /* An interactive run keeps real time; the tests keep a fixed date so that they are repeatable. */
    if (frames == 0 && current_mac_time() != 0)
        mac->rtc_seconds = current_mac_time();
    if (disk_in_memory)
    {
        mac->disk.read = overlay_read;
        mac->disk.write = overlay_write;
        mac->disk.ctx = ov;
        mac->disk.blocks = ov->blocks;
    }
    else
    {
        mac->disk.read = drive_read;
        mac->disk.write = drive_write;
        mac->disk.ctx = drive;
        mac->disk.blocks = drive->block_count > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)drive->block_count;
        mac->disk.read_only = drive->ops->write == NULL;
    }
    if (audio_out != NULL)
    {
        sound_resampler_reset(&audio_resampler);
        audio_count = 0;
        mac->audio = audio_collect;
    }
    mac_reset(mac);

    kernel_printf("%s\n", have_screen ? "mac: running (` stops)"
                                      : "mac: running without a screen");

    /* From here on the screen is the Mac's: the console keeps what it is told (dmesg, serial) but does not draw it. */
    if (have_screen && fbcon_display() != NULL)
    {
        fbcon_suspend();
        screen_taken = true;
    }

    uint64_t paced_start = hal_timer_ns();

    for (uint32_t frame = 1; frames == 0 || frame <= frames; frame++)
    {
#ifdef KERNEL_MAC_TEST_FOLDER
        for (unsigned int i = 0; i < sizeof(test_script) / sizeof(test_script[0]); i++)
        {
            if (test_script[i].frame == frame)
            {
                mac_mouse_warp(mac, test_script[i].x, test_script[i].y);
                mac_mouse_move(mac, 0, 0, test_script[i].down);
            }
        }
#endif
#ifdef KERNEL_MAC_TEST_BEEP
        /* make test-mac-sound: let the system beep once. */
        if (frame == KERNEL_MAC_TEST_BEEP)
            mac_sysbeep(mac);
#endif
        uint64_t emulate_start = hal_timer_ns();
        uint32_t instructions_before = mac->insn_count;

        mac_run(mac, (int)MAC_FRAME_CYCLES);

        perf.emulate_ns += hal_timer_ns() - emulate_start;
        perf.instructions += (uint32_t)(mac->insn_count - instructions_before);
        perf.frames++;

        if (audio_out != NULL)
            audio_flush(audio_out);

        if (have_screen && frame % SHOW_EVERY == 0)
        {
            uint64_t render_start = hal_timer_ns();
            int rendered = mac_render(mac, pixels);

            perf.render_ns += hal_timer_ns() - render_start;

            if (rendered == 0)
                show(&fb, pixels, scale);
        }
        if (handle_input(mac) && frames == 0)
        {
            break;
        }

        if (frames == 0)
        {
            pace(&paced_start, frame);

            /*
             * A report every 600 frames (10 s): how the Mac's time compares with ours
             * (frames and real milliseconds), and what its clock says (the Time
             * global, low memory 0x20C: seconds since 1904).
             */
            if (frame % 600 == 0)
                kernel_printf("mac: %u frames, %u ms, clock %u\n", frame,
                              (unsigned int)((hal_timer_ns() - paced_start) / 1000000u),
                              (unsigned int)mac_bus_read(mac, 0x20C, 4));
        }
    }
    uint64_t wall_ns = hal_timer_ns() - paced_start;

    if (mac_render(mac, pixels) == 0)
    {
        last_crc = crc32(pixels, MAC_SCREEN_W * MAC_SCREEN_H);
        if (have_screen)
        {
            show(&fb, pixels, scale);
        }
    }
    status = 0;

    /* A test run (a fixed number of frames) leaves the last picture on the screen for the screenshot. */
    if (screen_taken && frames == 0)
    {
        release_screen(&original);
        screen_taken = false;
    }

    report_performance(wall_ns);

out:
    if (screen_taken && frames == 0)
        release_screen(&original);

    kfree(shown);
    shown = NULL;
    shown_valid = false;

    kfree(audio_out);

    if (ov != NULL)
    {
        for (uint32_t i = 0; i < MAX_CHUNKS; i++)
        {
            kfree(ov->chunk[i]);
        }
        kfree(ov->slot);
    }
    kfree(ov);
    kfree(pixels);
    kfree(vram);
    kfree(rom);
    kfree(mac);
    return status;
}


uint32_t mac_machine_screen_crc(void)
{
    return last_crc;
}
