/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_VIRTIO_NET_VIRTIO_NET_H
#define DRIVERS_VIRTIO_NET_VIRTIO_NET_H

#include "hal/pci.h"

/*
 * virtio-net: the network card of virtual machines (QEMU, KVM, VirtualBox,
 * cloud hosts) that is made for them: no emulated hardware quirks, just
 * rings of buffers shared with the host. This driver speaks the "legacy"
 * (transitional) PCI interface: the register block in I/O BAR 0 of PCI
 * 1AF4:1000. Reference: Virtual I/O Device (VIRTIO) specification 1.1,
 * section 4.1.4.8 "Legacy Interfaces: A Note on PCI Device Layout", and 5.1
 * "Network Device".
 */
extern const hal_pci_driver_t virtio_net_pci_driver;

#endif
