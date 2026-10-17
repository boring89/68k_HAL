/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "signal.h"

#include "sched.h"

#include "hal/usermode.h"

#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/printf/printf.h"

#define SIG_DFL 0u
#define SIG_IGN 1u

/* sa_flags (Linux asm-generic/signal-defs.h and arch/x86/include/uapi/asm/signal.h) */
#define SA_SIGINFO 0x00000004u
#define SA_RESTORER 0x04000000u
#define SA_NODEFER 0x40000000u
#define SA_RESETHAND 0x80000000u

#define BIT(sig) (1u << ((sig) - 1))

/* Signals whose default action is to do nothing. */
#define DEFAULT_IGNORED (BIT(17) | BIT(18) | BIT(23) | BIT(28))
/* Stop signals: stopping is not supported, so they do nothing either. */
#define STOP_SIGNALS (BIT(19) | BIT(20) | BIT(21) | BIT(22))
/* These two can be neither caught, blocked nor ignored. */
#define UNBLOCKABLE (BIT(SIGNAL_KILL) | BIT(SIGNAL_STOP))

/* The kernel's struct sigaction on i386: 20 bytes. */
typedef struct
{
    uint32_t handler;
    uint32_t flags;
    uint32_t restorer;
    uint32_t mask[2];
} kernel_sigaction_t;

/* What is put on the user stack when a handler is called; sigreturn reads it back. */
typedef struct
{
    uint32_t return_address; /* the restorer */
    uint32_t signal;         /* the handler's first argument */
    uint32_t info_pointer;   /* SA_SIGINFO: second and third arguments */
    uint32_t context_pointer;
    hal_user_regs_t saved;
    uint32_t saved_mask;
    uint32_t info[32];       /* zeros: nobody fills siginfo and ucontext yet */
    uint32_t context[48];
} signal_frame_t;

static int ignored(const user_process_t *p, int sig)
{
    uint32_t handler = p->sig_action[sig].handler;

    if (handler == SIG_IGN)
        return 1;

    return handler == SIG_DFL && ((DEFAULT_IGNORED | STOP_SIGNALS) & BIT(sig));
}

void signal_send(user_process_t *p, int sig)
{
    if (p == NULL || sig < 1 || sig >= USER_SIGNAL_COUNT)
        return;

    if (sig != SIGNAL_KILL && ignored(p, sig))
        return;

    p->sig_pending |= BIT(sig);
    sched_notify();
}

int signal_send_group(int pgid, int sig)
{
    int count = 0;

    for (unsigned int i = 1; i <= USER_MAX_PROCS; i++)
    {
        user_process_t *p = sched_slot(i);

        if (p != NULL && p->pgid == pgid && p->state != PROC_ZOMBIE)
        {
            signal_send(p, sig);
            count++;
        }
    }

    return count;
}

static uint32_t deliverable(const user_process_t *p)
{
    return p->sig_pending & (~p->sig_blocked | UNBLOCKABLE);
}

int signal_interrupts(const user_process_t *p)
{
    return deliverable(p) != 0;
}

static int lowest(uint32_t set)
{
    for (int sig = 1; sig < USER_SIGNAL_COUNT; sig++)
    {
        if (set & BIT(sig))
            return sig;
    }

    return 0;
}

static void die(user_process_t *p, int sig)
{
    /* The keyboard's Ctrl-C and a broken pipe end a program quietly. */
    if (sig != SIGNAL_INT && sig != SIGNAL_QUIT && sig != SIGNAL_PIPE)
        kernel_printf("%s: killed by signal %u\n", p->name, (unsigned int)sig);

    sched_exit(0, sig);
}

void signal_check_fatal(void)
{
    user_process_t *p = user_current;
    uint32_t set = deliverable(p);

    for (int sig = 1; sig < USER_SIGNAL_COUNT && set != 0; sig++)
    {
        if (!(set & BIT(sig)))
            continue;

        set &= ~BIT(sig);

        if (sig == SIGNAL_KILL)
        {
            p->sig_pending &= ~BIT(sig);
            die(p, sig);
        }

        if (p->sig_action[sig].handler == SIG_DFL && !((DEFAULT_IGNORED | STOP_SIGNALS) & BIT(sig)))
        {
            p->sig_pending &= ~BIT(sig);
            die(p, sig);
        }
    }
}

int32_t signal_deliver(int32_t result)
{
    user_process_t *p = user_current;

    for (;;)
    {
        int sig = lowest(deliverable(p));

        if (sig == 0)
        {
            /* sigsuspend woke for a signal that turned out to need no handler: the old mask is back now. */
            if (p->sig_restore)
            {
                p->sig_blocked = p->sig_saved;
                p->sig_restore = 0;
            }

            return result;
        }

        p->sig_pending &= ~BIT(sig);

        user_sigaction_t *action = &p->sig_action[sig];

        if (sig == SIGNAL_KILL)
            die(p, sig);

        if (action->handler == SIG_IGN)
            continue;

        if (action->handler == SIG_DFL)
        {
            if ((DEFAULT_IGNORED | STOP_SIGNALS) & BIT(sig))
                continue;

            die(p, sig);
        }

        /* A handler: build the frame on the user stack and make the program return into the handler. */
        hal_user_regs_t regs;

        hal_user_regs_get(&regs);
        regs.eax = (uint32_t)result;

        uint32_t top = (regs.esp - sizeof(signal_frame_t)) & ~15u;
        /* The ABI wants (esp + 4) 16-byte aligned at the first instruction of a function. */
        uint32_t frame_address = top - 4;

        if (!user_range_writable(frame_address, sizeof(signal_frame_t)) || !(action->flags & SA_RESTORER))
            die(p, SIGNAL_SEGV);

        signal_frame_t *frame = (signal_frame_t *)(uintptr_t)frame_address;

        k_memset(frame, 0, sizeof(*frame));
        frame->return_address = action->restorer;
        frame->signal = (uint32_t)sig;
        frame->info_pointer = (uint32_t)(uintptr_t)frame->info;
        frame->context_pointer = (uint32_t)(uintptr_t)frame->context;
        frame->saved = regs;
        /* After sigsuspend the handler runs with the temporary mask; sigreturn brings the old one back. */
        frame->saved_mask = p->sig_restore ? p->sig_saved : p->sig_blocked;
        p->sig_restore = 0;
        frame->info[0] = (uint32_t)sig; /* si_signo */

        p->sig_blocked |= action->mask;

        if (!(action->flags & SA_NODEFER))
            p->sig_blocked |= BIT(sig);

        p->sig_blocked &= ~UNBLOCKABLE;

        if (action->flags & SA_RESETHAND)
            action->handler = SIG_DFL;

        regs.esp = frame_address;
        regs.eip = action->handler;
        hal_user_regs_set(&regs);

        return result;
    }
}

int32_t signal_sigreturn(int popped_signal)
{
    hal_user_regs_t now;

    hal_user_regs_get(&now);

    /*
     * The handler's return popped the return address, so the frame starts 4 bytes below esp; the
     * old sigreturn restorer (musl's __restore) also popped the signal number: 8 bytes.
     */
    uint32_t frame_address = now.esp - (popped_signal ? 8 : 4);

    if (!user_range_ok(frame_address, sizeof(signal_frame_t)))
        sched_exit(0, SIGNAL_SEGV);

    const signal_frame_t *frame = (const signal_frame_t *)(uintptr_t)frame_address;

    user_current->sig_blocked = frame->saved_mask & ~UNBLOCKABLE;
    hal_user_regs_set(&frame->saved);

    return (int32_t)frame->saved.eax;
}

int32_t signal_sigaction(int sig, uint32_t act, uint32_t old, uint32_t size)
{
    user_process_t *p = user_current;

    if (sig < 1 || sig >= USER_SIGNAL_COUNT || size < 4 || size > 8)
        return -K_EINVAL;

    if (old != 0)
    {
        kernel_sigaction_t out;

        k_memset(&out, 0, sizeof(out));
        out.handler = p->sig_action[sig].handler;
        out.flags = p->sig_action[sig].flags;
        out.restorer = p->sig_action[sig].restorer;
        out.mask[0] = p->sig_action[sig].mask;

        if (!user_range_writable(old, 12 + size))
            return -K_EFAULT;

        k_memcpy((void *)(uintptr_t)old, &out, 12 + size);
    }

    if (act != 0)
    {
        if (sig == SIGNAL_KILL || sig == SIGNAL_STOP)
            return -K_EINVAL;

        kernel_sigaction_t in;

        k_memset(&in, 0, sizeof(in));

        if (!user_range_ok(act, 12 + size))
            return -K_EFAULT;

        k_memcpy(&in, (const void *)(uintptr_t)act, 12 + size);

        p->sig_action[sig].handler = in.handler;
        p->sig_action[sig].flags = in.flags;
        p->sig_action[sig].restorer = in.restorer;
        p->sig_action[sig].mask = in.mask[0];

        /* Ignoring a signal throws away one that is waiting. */
        if (ignored(p, sig))
            p->sig_pending &= ~BIT(sig);
    }

    return 0;
}

int32_t signal_sigprocmask(int how, uint32_t set, uint32_t old, uint32_t size)
{
    user_process_t *p = user_current;

    if (size < 4 || size > 8)
        return -K_EINVAL;

    if (old != 0)
    {
        uint32_t out[2] = {p->sig_blocked, 0};

        if (!user_range_writable(old, size))
            return -K_EFAULT;

        k_memcpy((void *)(uintptr_t)old, out, size);
    }

    if (set != 0)
    {
        uint32_t in[2] = {0, 0};

        if (!user_range_ok(set, size))
            return -K_EFAULT;

        k_memcpy(in, (const void *)(uintptr_t)set, size);

        switch (how)
        {
        case 0: /* SIG_BLOCK */
            p->sig_blocked |= in[0];
            break;

        case 1: /* SIG_UNBLOCK */
            p->sig_blocked &= ~in[0];
            break;

        case 2: /* SIG_SETMASK */
            p->sig_blocked = in[0];
            break;

        default:
            return -K_EINVAL;
        }

        p->sig_blocked &= ~UNBLOCKABLE;
    }

    return 0;
}

int32_t signal_pending(uint32_t set, uint32_t size)
{
    uint32_t out[2] = {user_current->sig_pending & user_current->sig_blocked, 0};

    if (size < 4 || size > 8)
        return -K_EINVAL;

    if (!user_range_writable(set, size))
        return -K_EFAULT;

    k_memcpy((void *)(uintptr_t)set, out, size);

    return 0;
}

int32_t signal_pause(void)
{
    while (!signal_interrupts(user_current))
        sched_wait();

    return -K_EINTR;
}

int32_t signal_suspend(uint32_t mask_address, uint32_t size)
{
    user_process_t *p = user_current;
    uint32_t mask[2] = {0, 0};

    if (size < 4 || size > 8)
        return -K_EINVAL;

    if (!user_range_ok(mask_address, size))
        return -K_EFAULT;

    k_memcpy(mask, (const void *)(uintptr_t)mask_address, size);

    p->sig_saved = p->sig_blocked;
    p->sig_restore = 1;
    p->sig_blocked = mask[0] & ~UNBLOCKABLE;

    while (!signal_interrupts(p))
        sched_wait();

    /*
     * The temporary mask stays until signal_deliver has run the handler (with it) or found nothing to
     * run; the old mask comes back through sigreturn or there.
     */
    return -K_EINTR;
}

void signal_reset_for_exec(user_process_t *p)
{
    for (int i = 0; i < USER_SIGNAL_COUNT; i++)
    {
        if (p->sig_action[i].handler != SIG_IGN)
            p->sig_action[i].handler = SIG_DFL;

        p->sig_action[i].flags = 0;
        p->sig_action[i].restorer = 0;
        p->sig_action[i].mask = 0;
    }
}
