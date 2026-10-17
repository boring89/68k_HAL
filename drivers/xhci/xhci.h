/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_XHCI_XHCI_H
#define DRIVERS_XHCI_XHCI_H

#include "hal/pci.h"

/*
 * xHCI USB host controller (eXtensible Host Controller Interface for Universal Serial Bus,
 * Intel, revision 1.2; the structure follows docs/usb.md). Starts the controller, finds the
 * devices on its ports, and runs the HID boot keyboards and mice among them: their reports
 * become input events (kernel/input), like the PS/2 drivers' do. Other devices are listed
 * and left alone. One controller is used (the first one found).
 */
extern const hal_pci_driver_t xhci_pci_driver;

/* Devices found are listed in kernel/usb (usb_device_*). At most this many slots are used. */
#define XHCI_MAX_DEVICES 16

#endif
