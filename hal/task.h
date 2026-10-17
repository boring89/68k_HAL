/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HAL_TASK_H
#define HAL_TASK_H

#include <stdint.h>

/*
 * Kernel execution contexts: what a scheduler needs to run several flows of
 * control, each on its own kernel stack, and to give the CPU from one to the
 * next. Switching is cooperative: a context runs until it calls
 * hal_context_switch. Implemented once per architecture
 * (arch/<arch>/task/hal_task.c).
 */

/* A suspended context: the saved kernel stack pointer (the registers are on that stack). */
typedef struct
{
    uint32_t sp;
} hal_context_t;

/*
 * Prepare ctx so that the first hal_context_switch to it calls entry(arg) on the
 * given stack (stack_size bytes starting at stack). entry must never return. Interrupts
 * are off when it starts.
 */
void hal_context_init(hal_context_t *ctx, void *stack, uint32_t stack_size,
                      void (*entry)(void *), void *arg);

/* Save the running context in from and continue with to. Returns when somebody switches back to from. */
void hal_context_switch(hal_context_t *from, const hal_context_t *to);

/*
 * Tell the CPU where interrupts and system calls from user mode put their stack: the top (end)
 * of the kernel stack of the context that is about to run user code.
 */
void hal_task_set_kernel_stack(void *top);

/* Load base as the thread-local storage segment of the program about to run (0 = none). */
void hal_task_set_tls(uint32_t base);

/*
 * Floating point / vector register state of a user program: save it into area, load it from area,
 * or make area hold the state of a fresh CPU (this one clobbers the live state). The area is
 * HAL_FPU_AREA_SIZE bytes and 16-byte aligned.
 */
#define HAL_FPU_AREA_SIZE 512

void hal_fpu_save(void *area);
void hal_fpu_restore(const void *area);
void hal_fpu_reset_area(void *area);

#endif
