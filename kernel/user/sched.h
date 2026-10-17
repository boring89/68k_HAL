/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_USER_SCHED_H
#define KERNEL_USER_SCHED_H

#include "process.h"

/*
 * Cooperative scheduler (docs/process.md). Every process, and the kernel's own boot context
 * ("the kernel task", slot 0), is a flow of control with its own kernel stack. One runs until it
 * has to wait: then it calls sched_wait, which gives the CPU to another one. Nothing is ever
 * interrupted from outside, so kernel code needs no locks.
 *
 * Waiting is a loop in the caller:
 *
 *     while (!condition())
 *         sched_wait();
 *
 * sched_wait returns after something happened that may have changed the condition: a hardware
 * interrupt (key, timer, network card) or sched_notify() from another process. It may also
 * return for nothing: always check the condition again.
 */

/* Create the table with the kernel task in it. Call once at boot, after the heap. */
void sched_init(void);

/* Tell the waiting processes that something they may be waiting for has changed. */
void sched_notify(void);

/* Block until sched_notify or an interrupt; other processes run meanwhile. */
void sched_wait(void);

/* Let the other runnable processes have a turn first. */
void sched_yield(void);

/* The process with this pid, or NULL. */
user_process_t *sched_find(int pid);

/* The table slot i (0 = the kernel task), or NULL if empty. */
user_process_t *sched_slot(unsigned int i);

/* Number of processes in the table, zombies included, without the kernel task. */
unsigned int sched_process_count(void);

/*
 * A new process, child of parent, that does not run yet: pid, kernel stack and empty
 * address space (the caller fills it: user_map..., then sched_start). NULL if the table, the
 * heap or memory is full. Inherits the current directory and the signal settings from parent.
 */
user_process_t *sched_create(user_process_t *parent);

/* Make a process created with sched_create run: it starts in user mode at entry with the stack pointer sp. */
void sched_start(user_process_t *process, uint32_t entry, uint32_t sp);

/* Throw away a process that was created but never started (error paths), or a zombie that has been collected. */
void sched_destroy(user_process_t *process);

/*
 * fork (clone without sharing): a copy of the running process (memory, descriptors, signal settings) that continues in
 * user mode after the same system call with result 0 (on the stack child_sp if that is not 0). Returns the child's pid or -errno. Only
 * valid inside a system call.
 */
int32_t sched_fork(uint32_t child_sp);

/* End the running process: free its memory and descriptors, become a zombie. Never returns. */
void sched_exit(int exit_code, int term_signal) __attribute__((noreturn));

/*
 * wait4 for the running process: collect a zombie child (pid > 0: that one; -1: any;
 * 0: any of the same process group; < -1: any of group -pid). Returns its pid and stores the
 * status in the Linux wait format (exit << 8, or the signal number) in *status; 0 if nohang and
 * none has ended; -ECHILD or -EINTR.
 */
int32_t sched_wait_child(int pid, int *status, int nohang);

#endif
