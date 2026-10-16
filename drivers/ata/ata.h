/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_ATA_ATA_H
#define DRIVERS_ATA_ATA_H

/*
 * ATA hard disk on the primary IDE channel, master drive, read with PIO
 * (the CPU copies every word through the data port; no DMA, no interrupts)
 * and 28-bit LBA, so at most 128 GiB. Registered as the block device
 * "ata0" with 512-byte blocks. Read only.
 *
 * Returns 0 if a disk was found and registered, or -1 if there is no ATA
 * disk there (nothing attached, an ATAPI CD-ROM, a SATA device, a broken
 * answer) or registering failed.
 */
int ata_init(void);

#endif
