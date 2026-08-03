; Copyright (C) 2026 YuanChi Hsieh
; SPDX-License-Identifier: GPL-3.0-or-later

bits 32

global gdt_init

section .text

gdt_init:
    lgdt [gdt_descriptor]

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x08:.flush

.flush:
    ret


section .rodata

gdt_start:

; null descriptor
dq 0

; kernel code segment
dw 0xffff
dw 0x0000
db 0x00
db 10011010b
db 11001111b
db 0x00

; kernel data segment
dw 0xffff
dw 0x0000
db 0x00
db 10010010b
db 11001111b
db 0x00

gdt_end:

gdt_descriptor:
dw gdt_end - gdt_start - 1
dd gdt_start

section .note.GNU-stack noalloc noexec nowrite progbits