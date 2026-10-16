; Copyright (C) 2026 YuanChi Hsieh
; SPDX-License-Identifier: GPL-3.0-or-later

section .multiboot
align 4

; Flags: bit 1 = memory info, bit 2 = video mode (Multiboot Specification
; 0.6.96, 3.1.2 and 3.1.4). The five address fields are only used when
; bit 16 is set, so they stay 0 (the kernel is an ELF file).
MB_MAGIC equ 0x1BADB002
MB_FLAGS equ 0x00000006

multiboot_header:
	dd MB_MAGIC
	dd MB_FLAGS
	dd -(MB_MAGIC + MB_FLAGS)
	dd 0, 0, 0, 0, 0
	dd 0            ; mode_type: linear graphics
	dd 640          ; width
	dd 480          ; height
	dd 32           ; depth

section .text

global _start

extern pc_entry
extern gdt_init

_start:
	mov esi, ebx
	mov edi, eax

	; The Multiboot spec leaves esp undefined, so set up our own stack.
	mov esp, stack_top

	call gdt_init

	push esi
	push edi
	call pc_entry

hang:
	cli
	hlt
	jmp hang

section .bss
align 16

stack_bottom:
	resb 16384
stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits