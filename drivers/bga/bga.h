/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_BGA_BGA_H
#define DRIVERS_BGA_BGA_H

#include "hal/pci.h"

#include <stdint.h>

/*
 * The Bochs VBE "BGA" display adapter: QEMU's standard VGA (PCI
 * 1234:1111), Bochs, and VirtualBox's VBoxVGA. Its DISPI registers let
 * software choose any resolution directly, with no BIOS call; the
 * frame buffer is BAR 0. Reference: the Bochs VBE Extensions
 * documentation (vbe_display_api) and OSDev Wiki "Bochs VBE Extensions".
 */
extern const hal_pci_driver_t bga_pci_driver;

/* True once the adapter has been found (hal_pci_probe_drivers ran). */
int bga_available(void);

/*
 * Switch to a 32 bits per pixel mode of width x height (up to what the
 * adapter reports, at least 640x480). On success returns 0 and stores the
 * physical address of the frame buffer (BAR 0) in *framebuffer; the
 * pitch is width * 4. Returns -1 if there is no adapter or the mode is not
 * supported.
 */
int bga_set_mode(uint32_t width, uint32_t height, uint32_t *framebuffer);

/* The largest width and height the adapter reports (0 without an adapter). */
void bga_limits(uint32_t *width, uint32_t *height);

#endif
