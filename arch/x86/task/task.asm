; Copyright (C) 2026 YuanChi Hsieh
; SPDX-License-Identifier: GPL-3.0-or-later

bits 32

global hal_context_switch
global task_entry_stub
global fork_return_stub
global hal_user_enter

section .text

; void hal_context_switch(hal_context_t *from, const hal_context_t *to)
; Save the callee-saved registers and the flags on the current stack, store
; the stack pointer in *from, switch to the stack in *to and restore its
; registers. The same layout is built by hal_context_init and hal_user_fork
; (hal_task.c): edi, esi, ebx, ebp, eflags, return address.
hal_context_switch:
    mov eax, [esp + 4]
    mov edx, [esp + 8]

    push ebp
    push ebx
    push esi
    push edi
    pushfd
    cli                             ; no interrupt while the stack pointer is in between

    mov [eax], esp
    mov esp, [edx]

    popfd
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; First code of a new context: the stack holds entry and arg above the
; return address that brought us here.
task_entry_stub:
    pop eax                         ; entry
    pop edx                         ; arg
    push edx
    call eax
.hang:
    cli
    hlt
    jmp .hang

; First code of a forked child: the stack holds a copy of the parent's
; interrupt frame (arch/x86/interrupt/interrupt_frame.h), with eax = 0.
; Leave like user_syscall_stub does.
fork_return_stub:
    popa
    add esp, 8                      ; int_no, err_code
    iret

; void hal_user_enter(uint32_t entry, uint32_t user_esp)
; Drop to ring 3 with IRET (Intel SDM Vol.3A 6.12.1). The kernel stack
; is whatever the TSS says (hal_task_set_kernel_stack).
hal_user_enter:
    mov esi, [esp + 4]              ; entry
    mov edi, [esp + 8]              ; user stack

    mov ax, 0x23                    ; user data selector
    mov ds, ax
    mov es, ax
    mov fs, ax

    push dword 0x23                 ; ss
    push edi                        ; esp
    push dword 0x202                ; eflags: IF set, reserved bit 1
    push dword 0x1B                 ; cs
    push esi                        ; eip

    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    iret

section .note.GNU-stack noalloc noexec nowrite progbits
