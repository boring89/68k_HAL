/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_USB_HOST_USB_HOST_H
#define DRIVERS_USB_HOST_USB_HOST_H

#include "hal/pci.h"

/*
 * Finds USB host controllers (PCI class 0x0C, subclass 0x03) and registers
 * them with kernel/usb. It reads each controller's capability registers
 * (the version and the number of ports) and nothing else: no controller is
 * reset or started, so firmware that handles USB keyboards for the boot loader
 * keeps doing so. Controller layouts: xHCI 1.1 section 5.3 (capability
 * registers), EHCI 1.0 section 2.2, OHCI 1.0a section 7.4.1.
 */
extern const hal_pci_driver_t usb_host_pci_driver;

#endif
