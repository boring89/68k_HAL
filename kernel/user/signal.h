/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_USER_SIGNAL_H
#define KERNEL_USER_SIGNAL_H

#include "process.h"

/*
 * Signals (docs/process.md). The numbers are Linux's. A signal is sent by setting a bit in the
 * target's pending set; it is acted on by the target itself when a system call of it is about
 * to return to user mode (signal_deliver): the default action ends the process, a handler is
 * called on the user stack. A process that waits in the kernel returns -EINTR from the wait when
 * a signal is pending (signal_interrupts) so the signal does not wait for ever.
 */
#define SIGNAL_HUP 1
#define SIGNAL_INT 2
#define SIGNAL_QUIT 3
#define SIGNAL_ILL 4
#define SIGNAL_ABRT 6
#define SIGNAL_FPE 8
#define SIGNAL_KILL 9
#define SIGNAL_USR1 10
#define SIGNAL_SEGV 11
#define SIGNAL_USR2 12
#define SIGNAL_PIPE 13
#define SIGNAL_ALRM 14
#define SIGNAL_TERM 15
#define SIGNAL_CHLD 17
#define SIGNAL_CONT 18
#define SIGNAL_STOP 19

/* Send sig (1..31) to p. A signal that p ignores is dropped. */
void signal_send(user_process_t *p, int sig);

/* Send sig to every process of group pgid. Returns how many there were. */
int signal_send_group(int pgid, int sig);

/* True if p has a signal pending that would do something (not blocked, not ignored). */
int signal_interrupts(const user_process_t *p);

/*
 * Called when a system call is about to return result to user mode: take one pending signal and
 * act on it. May not return (the process ends). Returns the value to give to user mode.
 */
int32_t signal_deliver(int32_t result);

/* The system calls (arguments as the program gave them). */
int32_t signal_sigaction(int sig, uint32_t act, uint32_t old, uint32_t size);
int32_t signal_sigprocmask(int how, uint32_t set, uint32_t old, uint32_t size);
int32_t signal_sigreturn(int popped_signal);
int32_t signal_suspend(uint32_t mask_address, uint32_t size);
int32_t signal_pause(void);
int32_t signal_pending(uint32_t set, uint32_t size);

/* exec: handlers go back to the default action, ignored signals stay ignored. */
void signal_reset_for_exec(user_process_t *p);

#endif
