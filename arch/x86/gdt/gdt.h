/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_GDT_GDT_H
#define ARCH_X86_GDT_GDT_H

#include <stdint.h>

/*
 * Segment selectors (index * 8 | RPL), matching the table in gdt.asm.
 * Intel SDM Vol.3A 3.4.2 (selectors) and 3.4.5 (descriptors).
 */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE 0x1B /* ring 3 */
#define GDT_USER_DATA 0x23
#define GDT_TSS 0x28
#define GDT_TLS 0x33

/*
 * Install the task state segment (Intel SDM Vol.3A 7.2.1) and load the
 * task register. The CPU reads ss0:esp0 from it whenever an interrupt
 * or int 0x80 arrives from ring 3. Call once.
 */
void gdt_init_tss(void);

/* Where interrupts from ring 3 put their stack (the TSS's esp0). */
void gdt_set_kernel_stack(uint32_t esp0);

/*
 * Point the thread-local storage descriptor (selector GDT_TLS) at
 * base. The limit is the whole 4 GiB, like Linux's set_thread_area with
 * limit_in_pages.
 */
void gdt_set_tls(uint32_t base);

#endif
