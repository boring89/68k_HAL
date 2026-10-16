/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "machine.h"

#include "adbkeys.h"
#include "emu/mac/mac.h"
#include "emu/m68k/vm.h"
#include "hal/boot.h"
#include "hal/framebuffer.h"
#include "kernel/input/event_buffer.h"
#include "kernel/keyboard/keycode.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/m68k/guest.h"
#include "kernel/memory/heap.h"
#include "kernel/printf/printf.h"

#include <stddef.h>

#define ROM_SIZE 0x80000u
#define VRAM_SIZE 0x80000u
#define VRAM_BASE 0xFBB08000u       /* where the ROM's tables put the video RAM */
#define RAM_SIZE (8u * 1024 * 1024)
#define RTC_START 0xD0000000u       /* a fixed date: seconds since 1904 */
#define SHOW_EVERY 6u               /* frames between two redraws */
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


/* The loader's string for a module is "<file> <args>" (QEMU) or the line
 * after "module" (GRUB): the module is the one whose last word is 'name'. */
static const hal_boot_module_t *find_module(const char *name,
                                            hal_boot_module_t *storage)
{
    size_t wanted = k_strlen(name);

    for (unsigned int i = 0; hal_boot_module(i, storage) == 0; i++)
    {
        size_t length = k_strlen(storage->name);

        if (length >= wanted &&
            k_strcmp(storage->name + length - wanted, name) == 0 &&
            (length == wanted || storage->name[length - wanted - 1] == ' '))
        {
            return storage;
        }
    }
    return NULL;
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


/* Copy the rendered screen into the framebuffer, centered. */
static void show(const hal_framebuffer_t *fb, const uint32_t *pixels)
{
    uint32_t x0 = fb->width > MAC_SCREEN_W ? (fb->width - MAC_SCREEN_W) / 2 : 0;
    uint32_t y0 = fb->height > MAC_SCREEN_H ? (fb->height - MAC_SCREEN_H) / 2 : 0;
    uint32_t w = fb->width < MAC_SCREEN_W ? fb->width : MAC_SCREEN_W;
    uint32_t h = fb->height < MAC_SCREEN_H ? fb->height : MAC_SCREEN_H;

    for (uint32_t y = 0; y < h; y++)
    {
        k_memcpy(fb->base + (y0 + y) * fb->pitch + x0 * 4,
                 pixels + y * MAC_SCREEN_W, w * 4);
    }
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


int mac_machine_run(uint32_t frames)
{
    hal_boot_module_t rom_module;
    hal_boot_module_t disk_module;
    hal_framebuffer_t fb;
    bool have_screen = hal_framebuffer_get(&fb) == 0;
    mac_t *mac = kmalloc(sizeof *mac);
    uint8_t *rom = kmalloc(ROM_SIZE);
    uint8_t *vram = kmalloc(VRAM_SIZE);
    uint32_t *pixels = kmalloc(MAC_SCREEN_W * MAC_SCREEN_H * 4);
    overlay_t *ov = kmalloc(sizeof *ov);
    int status = -1;

    if (mac == NULL || rom == NULL || vram == NULL || pixels == NULL ||
        ov == NULL)
    {
        kernel_printf("%s\n", "mac: out of memory");
        goto out;
    }
    if (find_module(MAC_MODULE_ROM, &rom_module) == NULL ||
        find_module(MAC_MODULE_DISK, &disk_module) == NULL)
    {
        kernel_printf("%s\n", "mac: needs the boot modules mac_rom and mac_disk");
        goto out;
    }
    if (rom_module.length != ROM_SIZE)
    {
        kernel_printf("%s\n", "mac: mac_rom must be 512 KiB");
        goto out;
    }

    /* The ROM is patched in a copy: the module is read-only. */
    k_memcpy(rom, (const void *)(uintptr_t)rom_module.base, ROM_SIZE);
    if (mac_rom_patch(rom, ROM_SIZE) != 0)
    {
        kernel_printf("%s\n", "mac: mac_rom is not a Mac IIci ROM (368CADFE)");
        goto out;
    }

    k_memset(ov, 0, sizeof *ov);
    ov->image = (const uint8_t *)(uintptr_t)disk_module.base;
    ov->blocks = (uint32_t)(disk_module.length / BLOCK_SIZE);
    ov->slot = kmalloc((size_t)ov->blocks * sizeof(uint16_t));
    if (ov->slot == NULL)
    {
        kernel_printf("%s\n", "mac: out of memory");
        goto out;
    }
    k_memset(ov->slot, 0, (size_t)ov->blocks * sizeof(uint16_t));

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
    mac->disk.read = overlay_read;
    mac->disk.write = overlay_write;
    mac->disk.ctx = ov;
    mac->disk.blocks = ov->blocks;
    mac_reset(mac);

    kernel_printf("%s\n", have_screen ? "mac: running (` stops)"
                                      : "mac: running without a screen");
    for (uint32_t frame = 1; frames == 0 || frame <= frames; frame++)
    {
        mac_run(mac, (int)MAC_FRAME_CYCLES);
        if (have_screen && frame % SHOW_EVERY == 0 &&
            mac_render(mac, pixels) == 0)
        {
            show(&fb, pixels);
        }
        if (handle_input(mac) && frames == 0)
        {
            break;
        }
    }
    if (mac_render(mac, pixels) == 0)
    {
        last_crc = crc32(pixels, MAC_SCREEN_W * MAC_SCREEN_H);
        if (have_screen)
        {
            show(&fb, pixels);
        }
    }
    status = 0;

out:
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
