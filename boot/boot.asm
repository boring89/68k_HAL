section .text

global _start
extern kernel_main

_start:
	call kernel_main

hang:
	hlt
	jmp hang

section .note.GNU-stack noalloc noexec nowrite progbits