section .text

global _start

_start:
	call kernel_main

hang:
	hlt
	jmp hang
