bits 32

global isr0
extern interrupt_handler

section .text

isr0:
    cli
    
    push 0x30

    pusha

    push esp
    call interrupt_handler
    add esp,  4

    popa

    add esp, 4

    sti
    iret

section .note.GNU-stack noalloc noexec nowrite progbits