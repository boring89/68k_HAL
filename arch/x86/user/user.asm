; Copyright (C) 2026 YuanChi Hsieh
; SPDX-License-Identifier: GPL-3.0-or-later

bits 32

global user_syscall_stub
global user_run_asm
global user_leave_asm

extern user_syscall_c
extern gdt_set_kernel_stack

section .text

; int 0x80 from ring 3 (the gate has DPL 3, see idt.c). The stack
; layout equals the exception stubs' (interrupt_frame.h): the CPU pushed
; ss, esp, eflags, cs, eip because of the privilege change (Intel SDM
; Vol.3A 6.12.1, Figure 6-4).
user_syscall_stub:
    push dword 0                    ; err_code
    push dword 0x80                 ; int_no
    pusha
    cld                             ; the C ABI wants DF clear; user code may have set it

    push esp
    call user_syscall_c
    add esp, 4

    popa
    add esp, 8
    iret

; void user_run_asm(uint32_t entry, uint32_t user_esp)
; Save the callee-saved registers, tell the TSS to put interrupt
; stacks right below them, and drop to ring 3 with IRET. The registers
; are restored by user_leave_asm, which makes this function "return"
; once, later, from the system call that ends the program.
user_run_asm:
    push ebp
    push ebx
    push esi
    push edi

    mov esi, [esp + 20]             ; entry
    mov edi, [esp + 24]             ; user stack
    mov [user_saved_ksp], esp

    push esp
    call gdt_set_kernel_stack
    add esp, 4

    mov ax, 0x23                    ; user data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

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

; void user_leave_asm(int status): abandon the kernel stack of the
; system call and return from user_run_asm with status.
user_leave_asm:
    mov eax, [esp + 4]
    mov esp, [user_saved_ksp]

    mov cx, 0x10                    ; kernel data selector
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

section .bss
user_saved_ksp:
    resd 1

section .note.GNU-stack noalloc noexec nowrite progbits
