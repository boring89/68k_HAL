bits 32

global isr0
extern interrupt_handler

section .text

isr0:
    pusha

    push esp
    call interrupt_handler
    add esp,  4

    popa
    iret