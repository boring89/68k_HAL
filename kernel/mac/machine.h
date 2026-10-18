/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MAC_MACHINE_H
#define KERNEL_MAC_MACHINE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The Macintosh IIci (emu/mac) running inside the kernel.
 *
 * It needs two files from the boot loader (hal/boot.h modules), which are the
 * user's own and never part of this repository:
 *   mac_rom   the 512 KiB Mac IIci ROM image (checksum 368CADFE)
 *   mac_disk  a raw HFS disk image with a System Folder
 * and, for a picture, a graphics mode (32 bpp framebuffer). Without a
 * framebuffer the machine runs without showing anything.
 */

#define MAC_MODULE_ROM "mac_rom"
#define MAC_MODULE_DISK "mac_disk"

/* CPU cycles per call of the emulator and per shown frame: about 1/60 s of
 * Mac time (the CPU runs at 25 MHz). A multiple of the emulator's time
 * slice, so that a run is the same however it is cut into frames. */
#define MAC_FRAME_CYCLES 416000u

/*
 * Run the Mac for 'frames' frames, or until the grave accent key (`) is
 * pressed if 'frames' is 0. Keyboard and mouse events go to the Mac. Returns 0, or -1 if it cannot start (the reason is written to the
 * terminal). The screen is drawn every few frames.
 */
int mac_machine_run(uint32_t frames);

/*
 * The Mac's screen is a virtual NuBus video card (emu/mac, docs/nubus_video.md) with resolutions from
 * 640x480 to 3840x2160; the Mac can switch between them (Monitors). The picture is enlarged to the physical
 * screen in steps of a half: 'halves' = 2 is 1x, 3 is 1.5x, 4 is 2x ... up to 8 (4x); 0 = the largest that
 * fits. During the run Ctrl+Alt+Page Up / Page Down / Home change it. The video mode of the screen is never
 * changed.
 */
void mac_machine_set_scale_halves(uint32_t halves);

/*
 * The resolution the card starts in. 0 x 0 (the default) = the one that fills the screen best with an
 * enlargement of 2x, 1.5x or 1x (2560x1440: 1280x720 at 2x). Returns -1 if the card has no such mode.
 */
int mac_machine_set_resolution(uint32_t width, uint32_t height);

/* true: no NuBus card; the IIci's built-in video (640x480, 8 bits at most) is the screen. */
void mac_machine_set_builtin(bool builtin);

/* CRC-32 of the last rendered screen (see tools/mac_run.c, which prints the
 * same number for the same number of frames). */
uint32_t mac_machine_screen_crc(void);

#endif
