; Copyright (C) 2026 YuanChi Hsieh
; SPDX-License-Identifier: GPL-3.0-or-later

bits 32

global irq_stub_table
extern irq_handler

; Entry stubs for IRQ lines 0-15. Each pushes its line number so every
; IRQ reaches irq_common_stub with the same stack layout.

%macro IRQ 1
irq%1:
    push dword %1
    jmp irq_common_stub
%endmacro

section .text

irq_common_stub:
    ; Interrupt gates clear IF on entry (Intel SDM Vol.3 6.12.1.3).
    pusha
    cld

    push dword [esp + 32]
    call irq_handler
    add esp, 4

    popa

    ; Drop the line number.
    add esp, 4
    iret

%assign i 0
%rep 16
IRQ i
%assign i i + 1
%endrep

section .rodata

irq_stub_table:
%assign i 0
%rep 16
    dd irq %+ i
%assign i i + 1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
