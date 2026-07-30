section .multiboot
align 4

multiboot_header:
	dd  0x1BADB002
	dd 0x0
	dd -(0x1BADB002 + 0x0)

section .text

global _start
extern kernel_main

_start:
	mov dword [0xb8000], 0x07210748
	call kernel_main

hang:
	cli
	hlt
	jmp hang

section .note.GNU-stack noalloc noexec nowrite progbits