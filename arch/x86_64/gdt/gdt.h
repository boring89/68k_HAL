/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_64_GDT_GDT_H
#define ARCH_X86_64_GDT_GDT_H

#include <stdint.h>

/*
 * Segment selectors (index * 8 | RPL). Intel SDM Vol.3A 3.4.2 (selectors), 3.4.5 (descriptors),
 * 5.2.1 (the 64-bit mode GDT), 7.7 (the 64-bit TSS).
 *
 * User programs are i386 programs: they run in compatibility mode (IA-32e mode with a code
 * segment whose L bit is 0), so the user code segment is a 32-bit one. The selectors of the
 * segments that the i386 kernel has keep their values.
 */
#define GDT_KERNEL_CODE 0x08 /* 64-bit code, ring 0 */
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE 0x1B   /* 32-bit code, ring 3 (compatibility mode) */
#define GDT_USER_DATA 0x23
#define GDT_TLS 0x33         /* 0x30 | 3 */
#define GDT_TSS 0x38         /* a 64-bit TSS descriptor takes two slots (16 bytes) */

/* IST slots (SDM Vol.3A 6.14.5): own stacks for the exceptions that must not use a broken stack. */
#define GDT_IST_DOUBLE_FAULT 1
#define GDT_IST_NMI 2

/* Load the kernel's GDT and reload the segment registers. First thing the kernel does. */
void gdt_init(void);

/*
 * Install the task state segment and load the task register. The CPU reads rsp0 from it whenever
 * an interrupt or int 0x80 arrives from ring 3, and the IST stacks for the double fault and NMI.
 * Call once.
 */
void gdt_init_tss(void);

/* Where interrupts from ring 3 put their stack (the TSS's rsp0). */
void gdt_set_kernel_stack(uintptr_t rsp0);

/* The rsp0 last set by gdt_set_kernel_stack. */
uintptr_t gdt_get_kernel_stack(void);

/*
 * Point the thread-local storage descriptor (selector GDT_TLS) at base. The limit is the whole
 * 4 GiB, like Linux's set_thread_area with limit_in_pages.
 */
void gdt_set_tls(uint32_t base);

#endif
