; Copyright (C) 2026 YuanChi Hsieh
; SPDX-License-Identifier: GPL-3.0-or-later

bits 64

global user_syscall_stub
global user_run_asm
global user_leave_asm

extern user_syscall_c
extern gdt_set_kernel_stack

%include "arch/x86_64/interrupt/registers.inc"

section .text

; int 0x80 from ring 3 (the gate has DPL 3, see idt.c), from a program in compatibility mode. The
; stack layout equals the exception stubs' (interrupt_frame.h).
user_syscall_stub:
    push qword 0                    ; err_code
    push qword 0x80                 ; int_no
    PUSH_REGISTERS
    cld                             ; the C ABI wants DF clear; user code may have set it

    mov rdi, rsp
    call user_syscall_c

    POP_REGISTERS
    add rsp, 16
    iretq

; int user_run_asm(uint32_t entry, uint32_t user_esp)   (edi = entry, esi = user stack)
; Save the callee-saved registers, tell the TSS to put interrupt stacks right below them, and
; drop to the user's 32-bit code segment with IRETQ. The registers are restored by
; user_leave_asm, which makes this function "return" once, later, from the system call that ends
; the program.
user_run_asm:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8                      ; keep rsp 16-byte aligned for the interrupt entries (6 pushes + return address + 8)

    mov r12d, edi                   ; entry
    mov r13d, esi                   ; user stack
    mov [rel user_saved_ksp], rsp

    mov rdi, rsp
    call gdt_set_kernel_stack

    mov ax, 0x23                    ; user data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push qword 0x23                 ; ss
    push r13                        ; rsp
    push qword 0x202                ; rflags: IF set, reserved bit 1
    push qword 0x1B                 ; cs: 32-bit user code
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

; void user_leave_asm(int status): abandon the kernel stack of the system call and return from
; user_run_asm with status.
user_leave_asm:
    mov eax, edi
    mov rsp, [rel user_saved_ksp]

    mov cx, 0x10                    ; kernel data selector
    mov ds, cx
    mov es, cx
    xor ecx, ecx
    mov fs, cx
    mov gs, cx

    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

section .bss
user_saved_ksp:
    resq 1

section .note.GNU-stack noalloc noexec nowrite progbits
