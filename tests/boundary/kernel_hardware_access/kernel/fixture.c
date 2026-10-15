/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Fixture for tools/check_boundary.sh: the "kernel_hardware_access" rule must
 * report this file. Never compiled.
 */

void fixture(void)
{
    asm volatile("hlt");
    outb(0x20, 0x20);
}
