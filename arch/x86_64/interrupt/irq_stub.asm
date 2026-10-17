; Copyright (C) 2026 YuanChi Hsieh
; SPDX-License-Identifier: GPL-3.0-or-later

bits 64

global irq_stub_table
global irq_spurious_stub
extern irq_handler
extern irq_user_return

; Entry stubs for IRQ lines 0-63. Each pushes a dummy error code and its line number, so an IRQ
; reaches irq_common_stub with the same stack layout as an exception (interrupt_frame.h).

%macro IRQ 1
irq%1:
    push qword 0
    push qword %1
    jmp irq_common_stub
%endmacro

%include "arch/x86_64/interrupt/registers.inc"

section .text

irq_common_stub:
    ; Interrupt gates clear IF on entry (Intel SDM Vol.3A 6.12.1.3).
    PUSH_REGISTERS
    cld

    mov rdi, [rsp + 15 * 8]         ; int_no: the line
    call irq_handler

    ; Returning to ring 3 (the saved CS has RPL 3)? Then the kernel may switch to another
    ; process first (preemption).
    test qword [rsp + 17 * 8 + 8], 3
    jz .back
    call irq_user_return
.back:
    POP_REGISTERS

    ; Drop the line number and the dummy error code.
    add rsp, 16
    iretq

; A spurious local APIC interrupt: nothing to acknowledge.
irq_spurious_stub:
    iretq

%assign i 0
%rep 64
IRQ i
%assign i i + 1
%endrep

section .rodata

irq_stub_table:
%assign i 0
%rep 64
    dq irq %+ i
%assign i i + 1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
