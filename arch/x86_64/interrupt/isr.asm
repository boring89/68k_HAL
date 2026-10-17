; Copyright (C) 2026 YuanChi Hsieh
; SPDX-License-Identifier: GPL-3.0-or-later

bits 64

global isr_stub_table
extern exception_handler

; CPU exception stubs for vectors 0-31.
;
; The CPU pushes an error code only for vectors 8, 10, 11, 12, 13, 14, 17, 21 and 30 (Intel SDM
; Vol.3A Ch.6, Table 6-1). For the others a dummy 0 is pushed so every exception reaches
; isr_common_stub with the same stack layout (see interrupt_frame.h).

%macro ISR_NOERR 1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
isr%1:
    push qword %1
    jmp isr_common_stub
%endmacro

%include "arch/x86_64/interrupt/registers.inc"

section .text

isr_common_stub:
    PUSH_REGISTERS
    cld

    mov rdi, rsp
    call exception_handler

    POP_REGISTERS

    ; Drop int_no and err_code.
    add rsp, 16
    iretq

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

section .rodata

isr_stub_table:
%assign i 0
%rep 32
    dq isr %+ i
%assign i i + 1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
