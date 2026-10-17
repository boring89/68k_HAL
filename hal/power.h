/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_POWER_H
#define HAL_POWER_H

/*
 * Turning the machine off or restarting it. Implemented once per platform
 * (platform/<platform>/power/hal_power.c). Neither function returns.
 */

/* Power off (on the PC: ACPI soft off, S5; virtual machines' special ports as a fallback; else halt). */
void hal_power_off(void) __attribute__((noreturn));

/* Restart (on the PC: the ACPI reset register, then the keyboard controller, then port 0xCF9, then a triple fault). */
void hal_power_reboot(void) __attribute__((noreturn));

#endif
