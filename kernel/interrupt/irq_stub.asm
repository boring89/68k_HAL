bits 32

global irq0

extern irq_handler

section .text

irq_common_stub:
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


section .note.GNU-stack noalloc noexec nowrite progbits