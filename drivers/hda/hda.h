/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_HDA_HDA_H
#define DRIVERS_HDA_HDA_H

#include "hal/pci.h"

/*
 * Intel High Definition Audio controllers (the "ICH6" HDA that QEMU's
 * intel-hda and most PCs have) with one codec. Plays 48 kHz 16-bit
 * stereo through the first output (line out, speaker or headphone) the
 * codec has. Specification: Intel High Definition Audio Specification
 * revision 1.0a. Matches any PCI multimedia/HD-audio device (class 4,
 * subclass 3). Registers a sound_device_t (kernel/sound/sound.h).
 */
extern const hal_pci_driver_t hda_pci_driver;

#endif
