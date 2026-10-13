; Copyright (C) 2026 YuanChi Hsieh
; SPDX-License-Identifier: GPL-3.0-or-later

section .multiboot
align 4

multiboot_header:
	dd 0x1BADB002
	dd 0x00000002
	dd -(0x1BADB002 + 0x00000002)

section .text

global _start

extern kernel_main
extern gdt_init

_start:
	mov esi, ebx
	mov edi, eax

	; The Multiboot spec leaves esp undefined, so set up our own stack.
	mov esp, stack_top

	call gdt_init

	push esi
	push edi
	call kernel_main

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