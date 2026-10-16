/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_MAC_MACHINE_H
#define KERNEL_MAC_MACHINE_H

#include <stdint.h>

/*
 * The Macintosh IIci (emu/mac) running inside the kernel.
 *
 * It needs two files from the boot loader (hal/boot.h modules), which are the
 * user's own and never part of this repository:
 *   mac_rom   the 512 KiB Mac IIci ROM image (checksum 368CADFE)
 *   mac_disk  a raw HFS disk image with a System Folder
 * and, for a picture, the graphics mode (640x480 32 bpp framebuffer). Without
 * a framebuffer the machine runs without showing anything.
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
 * Enlargement of the Mac's screen: each Mac pixel becomes scale x scale
 * screen pixels (2 = "HiDPI": 640x480 shown as 1280x960). 1 is the default
 * and never changes the video mode. For 2 or more, mac_machine_run switches
 * the display to 640*scale x 480*scale if the adapter allows (see
 * hal_framebuffer_set_mode), otherwise it uses the largest scale that fits
 * the current mode. 0 means "as large as fits the current mode".
 */
void mac_machine_set_scale(uint32_t scale);

/* CRC-32 of the last rendered screen (see tools/mac_run.c, which prints the
 * same number for the same number of frames). */
uint32_t mac_machine_screen_crc(void);

#endif
