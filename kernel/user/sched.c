/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sched.h"

#include "signal.h"

#include "hal/cpu.h"
#include "hal/irq.h"
#include "hal/mmu.h"
#include "hal/task.h"
#include "hal/usermode.h"

#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/memory/heap.h"
#include "kernel/panic/panic.h"
#include "kernel/usb/usb.h"

user_process_t *user_current;

/* Slot 0 is the kernel task; the others hold user processes. */
static user_process_t kernel_task;
static user_process_t *table[USER_MAX_PROCS + 1];

static int next_pid = 1;
static uint32_t event_count;

void sched_init(void)
{
    k_memset(&kernel_task, 0, sizeof(kernel_task));
    kernel_task.state = PROC_RUNNABLE;
    kernel_task.is_kernel = 1;
    kernel_task.space = hal_space_current();
    kernel_task.pgid = 0;
    k_strcpy(kernel_task.name, "kernel");
    k_strcpy(kernel_task.cwd, vfs_getcwd());

    table[0] = &kernel_task;
    user_current = &kernel_task;
}

/* A number that changes whenever a process may have something new to see. */
static uint32_t stamp(void)
{
    return event_count + hal_irq_count();
}

void sched_notify(void)
{
    event_count++;
}

user_process_t *sched_find(int pid)
{
    for (int i = 1; i <= USER_MAX_PROCS; i++)
    {
        if (table[i] != NULL && table[i]->pid == pid)
            return table[i];
    }

    return NULL;
}

user_process_t *sched_slot(unsigned int i)
{
    return i <= USER_MAX_PROCS ? table[i] : NULL;
}

unsigned int sched_process_count(void)
{
    unsigned int count = 0;

    for (int i = 1; i <= USER_MAX_PROCS; i++)
        count += table[i] != NULL;

    return count;
}

/* --- switching --- */

static void *fpu_area(user_process_t *p)
{
    return (void *)(((uintptr_t)p->fpu_raw + 15) & ~(uintptr_t)15);
}

static void switch_to(user_process_t *next)
{
    user_process_t *prev = user_current;

    prev->state = prev->state == PROC_ZOMBIE ? PROC_ZOMBIE : PROC_RUNNABLE;
    next->state = PROC_RUNNABLE;

    if (next == prev)
        return;

    if (!prev->is_kernel && prev->state != PROC_ZOMBIE)
        hal_fpu_save(fpu_area(prev));

    k_strcpy(prev->cwd, vfs_getcwd());
    vfs_set_cwd(next->cwd);

    user_current = next;
    hal_space_switch(next->space);

    if (!next->is_kernel)
    {
        hal_task_set_kernel_stack((uint8_t *)next->kernel_stack + USER_KERNEL_STACK_SIZE);
        hal_task_set_tls(next->tls_base);
        hal_fpu_restore(fpu_area(next));
    }

    hal_context_switch(&prev->context, &next->context);
}

/* Give the CPU to the next process that can run; sleep the CPU if none can. */
static void schedule(void)
{
    user_process_t *prev = user_current;
    int start = 0;

    for (int i = 0; i <= USER_MAX_PROCS; i++)
    {
        if (table[i] == prev)
            start = i;
    }

    for (;;)
    {
        usb_poll(); /* devices come and go, and a lost interrupt must not lose a key */

        uint32_t now = stamp();

        /* Round robin, starting after the process that was running; it comes last. */
        for (int k = 1; k <= USER_MAX_PROCS + 1; k++)
        {
            user_process_t *p = table[(start + k) % (USER_MAX_PROCS + 1)];

            if (p == NULL)
                continue;

            if (p->state == PROC_RUNNABLE || (p->state == PROC_WAITING && p->seen != now))
            {
                switch_to(p);

                return;
            }
        }

        /* Everybody waits and nothing happened: sleep until the next interrupt. */
        hal_cpu_enable_interrupts();
        hal_cpu_idle();
    }
}

void sched_wait(void)
{
    user_process_t *p = user_current;

    p->seen = stamp();
    p->state = PROC_WAITING;
    schedule();
}

void sched_yield(void)
{
    schedule();
}

/* --- creating and ending processes --- */

static int allocate_pid(void)
{
    for (int tries = 0; tries < 32768; tries++)
    {
        int pid = next_pid;

        next_pid = next_pid >= 32767 ? 2 : next_pid + 1;

        if (sched_find(pid) == NULL)
            return pid;
    }

    return -1;
}

user_process_t *sched_create(user_process_t *parent)
{
    int slot = -1;

    for (int i = 1; i <= USER_MAX_PROCS && slot < 0; i++)
    {
        if (table[i] == NULL)
            slot = i;
    }

    if (slot < 0)
        return NULL;

    int pid = allocate_pid();

    if (pid < 0)
        return NULL;

    user_process_t *p = kmalloc(sizeof(*p));

    if (p == NULL)
        return NULL;

    k_memset(p, 0, sizeof(*p));

    p->kernel_stack = kmalloc(USER_KERNEL_STACK_SIZE);
    p->space = hal_space_create();

    if (p->kernel_stack == NULL || p->space == 0)
    {
        if (p->kernel_stack != NULL)
            kfree(p->kernel_stack);

        kfree(p);

        return NULL;
    }

    p->pid = pid;
    p->ppid = parent->pid;
    p->pgid = parent->is_kernel ? pid : parent->pgid;
    k_strcpy(p->name, parent->name);
    k_strcpy(p->cwd, parent->is_kernel ? vfs_getcwd() : parent->cwd);

    /* Handlers do not survive exec, but SIG_IGN does; here the child starts as the parent is. */
    for (int i = 0; i < USER_SIGNAL_COUNT; i++)
        p->sig_action[i] = parent->sig_action[i];

    p->sig_blocked = parent->sig_blocked;
    p->state = PROC_FREE;
    table[slot] = p;

    return p;
}

void sched_destroy(user_process_t *p)
{
    for (int i = 1; i <= USER_MAX_PROCS; i++)
    {
        if (table[i] == p)
            table[i] = NULL;
    }

    KASSERT(p != user_current);

    hal_space_destroy(p->space);
    kfree(p->kernel_stack);
    kfree(p);
}

static void process_entry(void *arg)
{
    user_process_t *p = arg;

    hal_user_enter(p->start_entry, p->start_sp);
}

void sched_start(user_process_t *p, uint32_t entry, uint32_t sp)
{
    p->start_entry = entry;
    p->start_sp = sp;
    hal_context_init(&p->context, p->kernel_stack, USER_KERNEL_STACK_SIZE, process_entry, p);
    hal_fpu_reset_area(fpu_area(p));
    p->state = PROC_RUNNABLE;
    sched_notify();
}

int32_t sched_fork(uint32_t child_sp)
{
    user_process_t *parent = user_current;
    user_process_t *child = sched_create(parent);

    if (child == NULL)
        return -K_EAGAIN;

    int32_t status = user_copy_memory(child);

    if (status != 0)
    {
        user_discard(child);
        sched_destroy(child);

        return status;
    }

    for (int i = 0; i < USER_MAX_FDS; i++)
    {
        if (parent->fd[i] != NULL)
            child->fd[i] = vfs_file_ref(parent->fd[i]);
    }

    k_memcpy(child->fd_cloexec, parent->fd_cloexec, sizeof(child->fd_cloexec));
    child->tls_base = parent->tls_base;
    child->pgid = parent->pgid;

    /* The child's FPU state is the parent's as it is now. */
    hal_fpu_save(fpu_area(child));

    if (hal_user_fork(&child->context, child->kernel_stack, USER_KERNEL_STACK_SIZE, child_sp) != 0)
    {
        user_discard(child);
        sched_destroy(child);

        return -K_EAGAIN;
    }

    child->state = PROC_RUNNABLE;
    sched_notify();

    return child->pid;
}

void sched_exit(int exit_code, int term_signal)
{
    user_process_t *p = user_current;

    KASSERT(!p->is_kernel);

    user_release_memory();

    /* The children of the ended process are adopted by the kernel task (pid 0). */
    for (int i = 1; i <= USER_MAX_PROCS; i++)
    {
        if (table[i] != NULL && table[i]->ppid == p->pid)
            table[i]->ppid = 0;
    }

    p->exit_code = exit_code;
    p->term_signal = term_signal;
    p->state = PROC_ZOMBIE;

    user_process_t *parent = sched_find(p->ppid);

    if (parent != NULL)
        signal_send(parent, SIGNAL_CHLD);

    sched_notify();
    schedule();

    panic("a zombie was scheduled");
}

/* --- wait --- */

static int matches(const user_process_t *self, const user_process_t *child, int pid)
{
    if (child->ppid != self->pid)
        return 0;

    if (pid > 0)
        return child->pid == pid;

    if (pid == 0)
        return child->pgid == self->pgid;

    if (pid < -1)
        return child->pgid == -pid;

    return 1;
}

int32_t sched_wait_child(int pid, int *status, int nohang)
{
    user_process_t *self = user_current;

    for (;;)
    {
        int have_child = 0;

        for (int i = 1; i <= USER_MAX_PROCS; i++)
        {
            user_process_t *child = table[i];

            if (child == NULL || !matches(self, child, pid))
                continue;

            have_child = 1;

            if (child->state == PROC_ZOMBIE)
            {
                int32_t reaped = child->pid;

                if (status != NULL)
                    *status = child->term_signal != 0 ? child->term_signal : (child->exit_code & 0xFF) << 8;

                sched_destroy(child);

                return reaped;
            }
        }

        if (!have_child)
            return -K_ECHILD;

        if (nohang)
            return 0;

        if (signal_interrupts(self))
            return -K_EINTR;

        sched_wait();
    }
}
