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


/* What the next run asks for: enlargement in halves (0 = automatic), the card's resolution (-1 = automatic) and
 * whether to use the built-in video instead of the NuBus card. */
static uint32_t want_halves = 0;
static int want_resolution = -1;
static bool want_builtin = false;
void mac_machine_set_scale_halves(uint32_t halves)
{
    want_halves = halves <= FB_SCALE_HALVES_MAX && halves != 1 ? halves : 0;
}

int mac_machine_set_resolution(uint32_t width, uint32_t height)
{
    if (width == 0 && height == 0)
    {
        want_resolution = -1;
        return 0;
    }
    int res = mac_card_find_resolution(width, height);

    if (res < 0)
        return -1;
    want_resolution = res;
    return 0;
}

void mac_machine_set_builtin(bool builtin)
{
    want_builtin = builtin;
}

/*
 * How the Mac's picture is laid out on the screen: its size (the card's mode, which the Mac can change), the
 * enlargement in halves and where it starts. The picture itself (pixels) is kept up to date row by row: only
 * the rows the Mac changed are converted (mac_render_changes) and drawn.
 */
static struct
{
    uint32_t width, height;     /* the Mac's screen */
    uint32_t halves;            /* the enlargement now: 2 = 1x, 3 = 1.5x, 4 = 2x */
    uint32_t requested;         /* what the user asked for (0 = the largest that fits) */
    bool fits;                  /* false: the screen is smaller than the picture; the top left part is shown */
    uint32_t x0, y0;
    uint32_t *pixels;
    uint32_t *row;              /* one enlarged row */
    uint8_t *changed;           /* per row of the picture: drawn in the last render */
} view;

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

/* The largest enlargement (in halves, 8 = 4x at most) at which the picture fits the screen; 0 if none. */
static uint32_t largest_halves(const hal_framebuffer_t *fb, uint32_t width, uint32_t height)
{
    for (uint32_t h = FB_SCALE_HALVES_MAX; h >= 2; h--)
    {
        if (fb_scaled_size(width, h) <= fb->width && fb_scaled_size(height, h) <= fb->height)
            return h;
    }
    return 0;
}

/* Fill the screen with black (display memory: written only). */
static void clear_screen(const hal_framebuffer_t *fb)
{
    for (uint32_t y = 0; y < fb->height; y++)
        k_memset(fb->base + (size_t)y * fb->pitch, 0, (size_t)fb->width * 4);
}

/*
 * Lay the picture out again: at the start, when the Mac switches its resolution and when the enlargement
 * changes. Returns -1 if there is no memory for the picture.
 */
static int relayout(mac_t *mac, const hal_framebuffer_t *fb, bool have_screen)
{
    uint32_t width, height;

    mac_screen_size(mac, &width, &height);
    if (width != view.width || height != view.height || view.pixels == NULL)
    {
        kfree(view.pixels);
        kfree(view.changed);
        view.pixels = kmalloc((size_t)width * height * 4);
        view.changed = kmalloc(height);
        if (view.pixels == NULL || view.changed == NULL)
            return -1;
        view.width = width;
        view.height = height;
    }
    mac->card.all_dirty = true;

    if (!have_screen)
        return 0;

    uint32_t largest = largest_halves(fb, width, height);

    view.fits = largest != 0;
    view.halves = view.requested != 0 && view.requested <= largest ? view.requested : largest;
    if (!view.fits)
        view.halves = 2;

    uint32_t shown_w = fb_scaled_size(width, view.halves);
    uint32_t shown_h = fb_scaled_size(height, view.halves);

    view.x0 = view.fits ? (fb->width - shown_w) / 2 : 0;
    view.y0 = view.fits ? (fb->height - shown_h) / 2 : 0;
    kfree(view.row);
    view.row = kmalloc((size_t)shown_w * 4);
    if (view.row == NULL)
        return -1;

    clear_screen(fb);
    kernel_printf("mac: screen %ux%u, enlarged %u.%ux\n", width, height, view.halves / 2,
                  view.halves % 2 ? 5u : 0u);
    return 0;
}

/* Draw the rows of the picture that changed (view.changed) on the screen. */
static void show(const hal_framebuffer_t *fb)
{
    uint64_t start = hal_timer_ns();

    if (view.fits)
    {
        perf.rows += fb_blit_halves(fb->base + (size_t)view.y0 * fb->pitch + (size_t)view.x0 * 4, fb->pitch,
                                    view.pixels, view.changed, view.width, view.height, view.halves, view.row);
    }
    else
    {
        /* A smaller screen than the picture: the top left part, unscaled. */
        uint32_t w = fb->width < view.width ? fb->width : view.width;
        uint32_t h = fb->height < view.height ? fb->height : view.height;

        for (uint32_t y = 0; y < h; y++)
        {
            if (!view.changed[y])
                continue;

            k_memcpy(fb->base + (size_t)y * fb->pitch, view.pixels + (size_t)y * view.width, (size_t)w * 4);
            perf.rows++;
        }
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
    print_tenths(" (", rows10, " rows drawn of ");
    kernel_printf("%ux%u)", view.width, view.height);
    kernel_printf("\n");
}


/*
 * The enlargement hot keys: Ctrl+Alt+Page Up / Page Down make the picture half a step larger / smaller,
 * Ctrl+Alt+Home goes back to the largest that fits. Control-Command with these keys means nothing to the Mac,
 * and the kernel keeps these key presses to itself. Returns the new requested enlargement (halves, 0 =
 * largest), or -1 if the key is not one of them.
 */
static int scale_hot_key(keycode_t key, uint32_t halves)
{
    switch (key)
    {
    case KEY_PAGE_UP:
        return halves < FB_SCALE_HALVES_MAX ? (int)halves + 1 : (int)halves;
    case KEY_PAGE_DOWN:
        return halves > 2 ? (int)halves - 1 : 2;
    case KEY_HOME:
        return 0;
    default:
        return -1;
    }
}

/*
 * Pass keyboard and mouse events on to the Mac. The grave accent key (`) stops
 * the Mac: it is the one key the Mac side does not need. Returns true to stop;
 * *rescale is set when the enlargement was changed with a hot key.
 */
static bool handle_input(mac_t *mac, bool *rescale)
{
    input_event_t event;
    static bool ctrl_down, alt_down;

    while (input_event_pop(&event) == 0)
    {
        if (event.type == INPUT_EVENT_KEYBOARD)
        {
            int code;
            keycode_t key = event.data.keyboard.key;

            if (key == KEY_LEFT_CTRL || key == KEY_RIGHT_CTRL)
                ctrl_down = event.data.keyboard.pressed;
            if (key == KEY_LEFT_ALT || key == KEY_RIGHT_ALT)
                alt_down = event.data.keyboard.pressed;

            if (event.data.keyboard.pressed &&
                event.data.keyboard.key == KEY_GRAVE)
            {
                return true;
            }
            if (ctrl_down && alt_down && scale_hot_key(key, view.halves) >= 0)
            {
                if (event.data.keyboard.pressed)
                {
                    view.requested = (uint32_t)scale_hot_key(key, view.halves);
                    *rescale = true;
                }
                continue;
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
#elif defined(KERNEL_MAC_TEST_MILLIONS)
/*
 * make test-mac-perf: switch the NuBus card to Millions of colors with the
 * Monitors control panel (Apple menu > Control Panels, View > by Name,
 * Monitors, Colors, scroll, Millions), so that the run is measured in the
 * 32-bit mode. Positions as in tools/mac_monitors.sh (the windows open at the
 * same place on any screen from 1024x768 down to 1280x720).
 */
static const struct
{
    uint32_t frame;
    int x;
    int y;
    bool down;
} test_script[] = {
    {1200, 20, 9, true}, {1220, 70, 60, true}, {1240, 70, 60, false},          /* Control Panels */
    {2000, 130, 9, true}, {2020, 150, 59, true}, {2040, 150, 59, false},       /* View > by Name */
    {2400, 80, 170, true}, {2403, 80, 170, false}, {2406, 80, 170, true},      /* open Monitors */
    {2409, 80, 170, false},
    {3000, 116, 105, true}, {3005, 116, 105, false},                           /* Colors */
    {3050, 241, 128, true}, {3060, 241, 128, false},                           /* scroll down */
    {3100, 190, 130, true}, {3105, 190, 130, false},                           /* Millions */
};
#define KERNEL_MAC_TEST_SCRIPT
#endif
#ifdef KERNEL_MAC_TEST_FOLDER
#define KERNEL_MAC_TEST_SCRIPT
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
    mac_t *mac = kmalloc(sizeof *mac);
    uint8_t *rom = kmalloc(ROM_SIZE);
    uint8_t *vram = kmalloc(VRAM_SIZE);
    uint8_t *card_vram = want_builtin ? NULL : kmalloc(MAC_CARD_VRAM_SIZE);
    overlay_t *ov = kmalloc(sizeof *ov);
    have_screen = hal_framebuffer_get(&fb) == 0;
    original = fb;
    k_memset(&perf, 0, sizeof perf);
    k_memset(&view, 0, sizeof view);
    view.requested = want_halves;

    int16_t *audio_out = sound_get() != NULL ? kmalloc(AUDIO_FRAMES * 4) : NULL;
    int status = -1;

    if (!want_builtin && card_vram == NULL)
        kernel_printf("%s\n", "mac: no memory for the video card's 32 MiB: using the built-in video");

    if (mac == NULL || rom == NULL || vram == NULL || ov == NULL)
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

    if (card_vram != NULL)
    {
        /* The card's first resolution: as asked, or the one that fills this screen best (with its enlargement). */
        uint32_t res = 0;

        if (want_resolution >= 0)
        {
            res = (uint32_t)want_resolution;
        }
        else if (have_screen)
        {
            uint16_t widths[16], heights[16];
            uint32_t count = mac_card_resolution_count < 16 ? mac_card_resolution_count : 16;
            uint32_t halves;

            for (uint32_t i = 0; i < count; i++)
            {
                widths[i] = mac_card_resolutions[i].width;
                heights[i] = mac_card_resolutions[i].height;
            }
            if (fb_choose_size(widths, heights, count, fb.width, fb.height, &res, &halves) == 0 &&
                view.requested == 0)
                view.requested = halves;
        }
        k_memset(card_vram, 0, MAC_CARD_VRAM_SIZE);
        mac_card_attach(mac, card_vram, res, 0);
    }
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

    if (relayout(mac, &fb, have_screen) != 0)
    {
        kernel_printf("%s\n", "mac: out of memory");
        goto out;
    }

    uint64_t paced_start = hal_timer_ns();

    for (uint32_t frame = 1; frames == 0 || frame <= frames; frame++)
    {
#ifdef KERNEL_MAC_TEST_SCRIPT
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

        bool rescale = false;
        uint32_t width, height;

        /* The Mac switched its resolution: a new picture and layout. */
        mac_screen_size(mac, &width, &height);
        if (width != view.width || height != view.height)
            rescale = true;

        if (have_screen && frame % SHOW_EVERY == 0)
        {
            if (rescale && relayout(mac, &fb, have_screen) != 0)
                break;
            rescale = false;

            uint64_t render_start = hal_timer_ns();
            int rendered = mac_render_changes(mac, view.pixels, view.changed);

            perf.render_ns += hal_timer_ns() - render_start;

            if (rendered > 0)
                show(&fb);
        }
        if (handle_input(mac, &rescale) && frames == 0)
        {
            break;
        }
        if (rescale && relayout(mac, &fb, have_screen) != 0)
            break;

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

    /* The whole picture once more, for its CRC (the same as tools/mac_run prints). */
    if (relayout(mac, &fb, false) == 0 && mac_render_changes(mac, view.pixels, view.changed) >= 0)
    {
        last_crc = crc32(view.pixels, view.width * view.height);
        if (have_screen)
        {
            relayout(mac, &fb, true);
            mac_render_changes(mac, view.pixels, view.changed);
            show(&fb);
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

    kfree(view.pixels);
    kfree(view.changed);
    kfree(view.row);
    k_memset(&view, 0, sizeof view);
    kfree(card_vram);

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
    kfree(vram);
    kfree(rom);
    kfree(mac);
    return status;
}


uint32_t mac_machine_screen_crc(void)
{
    return last_crc;
}
