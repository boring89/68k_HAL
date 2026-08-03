; Copyright (C) 2026 YuanChi Hsieh
; SPDX-License-Identifier: GPL-3.0-or-later

bits 32

global irq0
global irq1

extern irq_handler

section .text

irq_common_stub:
    cli
    
    pusha

    push dword [esp + 32]
    call irq_handler
    add esp, 4

    popa

    add esp, 4
    iret


irq0:
    push 0
    jmp irq_common_stub

irq1:
    push 1
    jmp irq_common_stub

section .note.GNU-stack noalloc noexec nowrite progbits