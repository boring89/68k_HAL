; Copyright (C) 2026 YuanChi Hsieh
; SPDX-License-Identifier: GPL-3.0-or-later

; Start-up code for the application processors (APs). A SIPI makes an AP
; begin in 16-bit real mode at address vector * 4096 (Intel SDM Vol.3A
; 8.4.4, MP Initialization Protocol), so smp.c copies this block to
; TRAMP_BASE (below 1 MiB) before sending it. The code is position
; dependent on that address: every reference to the block's own labels
; is written as TRAMP_BASE + (label - ap_tramp_start).
;
; The AP goes real mode -> 32-bit protected mode with a flat GDT of its
; own, loads the stack and the C entry point the boot CPU stored in the
; data words at the end, and calls it. Paging stays off: the kernel is
; identity mapped, so its code and data are at the same addresses.

TRAMP_BASE equ 0x7000

global ap_tramp_start
global ap_tramp_end
global ap_tramp_stack
global ap_tramp_entry

section .text

bits 16
ap_tramp_start:
    cli
    xor ax, ax
    mov ds, ax                      ; CS was set by the SIPI; DS must be 0 for the absolute addresses below
    lgdt [TRAMP_BASE + (ap_tramp_gdt_desc - ap_tramp_start)]

    mov eax, cr0
    or al, 1                        ; CR0.PE
    mov cr0, eax

    jmp dword 0x08:(TRAMP_BASE + (ap_tramp_pm - ap_tramp_start))

bits 32
ap_tramp_pm:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esp, [TRAMP_BASE + (ap_tramp_stack - ap_tramp_start)]
    call [TRAMP_BASE + (ap_tramp_entry - ap_tramp_start)]

.hang:
    cli
    hlt
    jmp .hang

align 8
ap_tramp_gdt:
    dq 0
    dq 0x00CF9A000000FFFF           ; 0x08: code, base 0, limit 4 GiB, 32-bit
    dq 0x00CF92000000FFFF           ; 0x10: data
ap_tramp_gdt_desc:
    dw ap_tramp_gdt_desc - ap_tramp_gdt - 1
    dd TRAMP_BASE + (ap_tramp_gdt - ap_tramp_start)

; Written by the boot CPU in the copy, one AP at a time.
ap_tramp_stack:
    dd 0
ap_tramp_entry:
    dd 0
ap_tramp_end:

section .note.GNU-stack noalloc noexec nowrite progbits
