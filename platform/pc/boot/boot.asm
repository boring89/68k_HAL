; Copyright (C) 2026 YuanChi Hsieh
; SPDX-License-Identifier: GPL-3.0-or-later

section .multiboot
align 4

; Flags: bit 1 = memory info (Multiboot Specification 0.6.96, 3.1.2).
; Built with -d GRAPHICS (kernel-gfx.elf) the header also sets bit 2 and asks
; for a 640x480x32 linear framebuffer (3.1.4). GRUB follows the header and
; ignores gfxpayload for Multiboot 1, so a text-mode and a graphics-mode boot
; need two images that differ only in this header. The five address fields
; are only used when bit 16 is set, so they stay 0 (the kernel is an ELF file).
MB_MAGIC equ 0x1BADB002
%ifdef GRAPHICS
MB_FLAGS equ 0x00000006
%else
MB_FLAGS equ 0x00000002
%endif

multiboot_header:
	dd MB_MAGIC
	dd MB_FLAGS
	dd -(MB_MAGIC + MB_FLAGS)
%ifdef GRAPHICS
	dd 0, 0, 0, 0, 0
	dd 0            ; mode_type: linear graphics
%ifdef GFX_WIDTH
	dd GFX_WIDTH    ; width: as the Multiboot2 header (the loader falls back to a mode it has)
	dd GFX_HEIGHT   ; height
%else
	dd 640          ; width
	dd 480          ; height
%endif
	dd 32           ; depth
%endif

; The size the Multiboot2 header asks for (kernel-gfx.elf); 0 x 0 = no preference.
%ifndef GFX_WIDTH
GFX_WIDTH equ 0
GFX_HEIGHT equ 0
%endif

; Multiboot2 header (Multiboot2 Specification 2.0, 3.1). A loader that speaks
; Multiboot2 (GRUB's "multiboot2" command, the only way to get the ACPI RSDP
; and a framebuffer under UEFI) uses this one; the Multiboot 1 header above
; stays for QEMU -kernel and GRUB's "multiboot" command. Both headers can sit
; in the same image. The header must be 8-byte aligned.
align 8
MB2_MAGIC equ 0xE85250D6
mb2_header:
	dd MB2_MAGIC
	dd 0                              ; architecture: i386
	dd mb2_header_end - mb2_header    ; header length
	dd -(MB2_MAGIC + 0 + (mb2_header_end - mb2_header))
%ifdef GRAPHICS
	; Framebuffer tag: type 5, size 20, width, height, depth. 0 x 0 = no preference: the loader
	; keeps the mode it set up itself (grub.cfg: gfxmode, e.g. the monitor's own size under UEFI).
	; The Multiboot 1 header above asks for the same size (640x480 without GFX_WIDTH).
	align 8
	dw 5, 0
	dd 20
	dd GFX_WIDTH, GFX_HEIGHT, 32
%else
	; No preference (0 x 0 x 0 would mean "any"); the text image keeps VGA
	; text mode, so ask for the EGA text console (console flags tag, type 4,
	; flags bit 1 = EGA text supported).
	align 8
	dw 4, 0
	dd 12
	dd 2
%endif
	align 8
	dw 0, 0                           ; end tag
	dd 8
mb2_header_end:

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