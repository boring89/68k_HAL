; Copyright (C) 2026 YuanChi Hsieh
; SPDX-License-Identifier: GPL-3.0-or-later

bits 64

global hal_context_switch
global task_entry_stub
global fork_return_stub
global hal_user_enter

%include "arch/x86_64/interrupt/registers.inc"

section .text

; void hal_context_switch(hal_context_t *from, const hal_context_t *to)   (rdi = from, rsi = to)
; Save the callee-saved registers and the flags on the current stack, store the stack pointer in
; *from, switch to the stack in *to and restore its registers. The same layout is built by
; hal_context_init and hal_user_fork (hal_task.c): r15, r14, r13, r12, rbx, rbp, rflags, return address.
hal_context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    pushfq
    cli                             ; no interrupt while the stack pointer is in between

    mov [rdi], rsp
    mov rsp, [rsi]

    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; First code of a new context: the stack holds entry and arg above the return address that
; brought us here (hal_context_init keeps rsp 16-byte aligned for the call).
task_entry_stub:
    pop rax                         ; entry
    pop rdi                         ; arg
    call rax
.hang:
    cli
    hlt
    jmp .hang

; First code of a forked child: the stack holds a copy of the parent's interrupt frame
; (arch/x86_64/interrupt/interrupt_frame.h), with rax = 0. Leave like user_syscall_stub does.
fork_return_stub:
    POP_REGISTERS
    add rsp, 16                     ; int_no, err_code
    iretq

; void hal_user_enter(uint32_t entry, uint32_t user_esp)   (edi = entry, esi = user stack)
; Drop to the user's 32-bit code segment with IRETQ (Intel SDM Vol.3A 6.14.3). The kernel stack
; is whatever the TSS says (hal_task_set_kernel_stack).
hal_user_enter:
    mov r12d, edi                   ; entry
    mov r13d, esi                   ; user stack

    mov ax, 0x23                    ; user data selector
    mov ds, ax
    mov es, ax
    mov fs, ax

    push qword 0x23                 ; ss
    push r13                        ; rsp
    push qword 0x202                ; rflags: IF set, reserved bit 1
    push qword 0x1B                 ; cs
    push r12                        ; rip

    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
