/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_USER_USER_H
#define ARCH_X86_USER_USER_H

#include "arch/x86/interrupt/interrupt_frame.h"

#include <stdint.h>

/* The int 0x80 entry stub (user.asm), installed in the IDT by idt_init. */
void user_syscall_stub(void);

/* user.asm: see hal_user_run and hal_user_exit. */
int user_run_asm(uint32_t entry, uint32_t user_esp);
void user_leave_asm(int status) __attribute__((noreturn));

/* hal_user.c: an exception that happened in ring 3. Does not return. */
void user_fault(struct interrupt_frame *frame) __attribute__((noreturn));

#endif
