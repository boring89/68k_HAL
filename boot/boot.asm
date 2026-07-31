section .multiboot
align 4

multiboot_header:
	dd  0x1BADB002
	dd 0x0
	dd -(0x1BADB002 + 0x0)

section .text

global _start

extern kernel_main
extern gdt_init

_start:
	call gdt_init
	call kernel_main

hang:
	cli
	hlt
	jmp hang

section .note.GNU-stack noalloc noexec nowrite progbits