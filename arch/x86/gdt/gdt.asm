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


; Writable: the CPU sets the Accessed bit (bit 0 of the type field) of a
; descriptor when it loads a segment register (Intel SDM Vol.3A 3.4.5.1),
; so the GDT must not end up on a read-only page.
section .data

; 8-byte alignment gives the best performance (Intel SDM Vol.3A 3.5.1).
align 8
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