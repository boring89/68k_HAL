; Copyright (C) 2026 YuanChi Hsieh
; SPDX-License-Identifier: GPL-3.0-or-later

; Entry of the 64-bit kernel (ARCH=x86_64, docs/x86_64.md). The boot loader starts us in 32-bit
; protected mode (Multiboot 1 or 2, EAX = magic, EBX = boot information) with paging off. This
; code builds page tables, enters long mode (Intel SDM Vol.3A 9.8.5, OSDev Wiki "Setting Up Long
; Mode") and calls pc_entry in the higher half.
;
; The boot tables map the first 4 GiB of physical memory three times, with 2 MiB pages:
;   identity            0x0000000000000000   (this code runs there until the kernel has its own tables)
;   direct map (HHDM)   0xFFFF800000000000   (hal_phys_to_virt)
;   kernel image        0xFFFFFFFF80000000   (the first GiB; the kernel is linked there)

section .multiboot
align 4

; Multiboot 1 header (Multiboot Specification 0.6.96, 3.1): flags bit 1 = memory info; with
; -d GRAPHICS also bit 2 and a 640x480x32 framebuffer request. The five address fields stay 0
; (the kernel is an ELF file).
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
	dd 640          ; width
	dd 480          ; height
	dd 32           ; depth
%endif

%ifndef GFX_WIDTH
GFX_WIDTH equ 0
GFX_HEIGHT equ 0
%endif

; Multiboot2 header (Multiboot2 Specification 2.0, 3.1); architecture 0 = i386: the loader enters
; in 32-bit protected mode.
align 8
MB2_MAGIC equ 0xE85250D6
mb2_header:
	dd MB2_MAGIC
	dd 0
	dd mb2_header_end - mb2_header
	dd -(MB2_MAGIC + 0 + (mb2_header_end - mb2_header))
%ifdef GRAPHICS
	align 8
	dw 5, 0
	dd 20
	dd GFX_WIDTH, GFX_HEIGHT, 32
%else
	align 8
	dw 4, 0
	dd 12
	dd 2
%endif
	align 8
	dw 0, 0
	dd 8
mb2_header_end:

; ---------------------------------------------------------------------------------------------

PAGE_PRESENT_WRITE equ 0x3          ; P | R/W
PAGE_LARGE equ 0x80                 ; PS: a 2 MiB page in a page directory entry
CR4_PAE equ 0x20                    ; bit 5
MSR_EFER equ 0xC0000080
EFER_LME equ 0x100                  ; bit 8: long mode enable
EFER_NXE equ 0x800                  ; bit 11: no-execute enable
CR0_PE_PG_WP equ 0x80010001         ; PE (bit 0) | WP (bit 16) | PG (bit 31)
SERIAL equ 0x3F8

section .boot.text progbits alloc exec nowrite align=16
bits 32

global _start
extern pc_entry

; Print the character in the low byte of the string at ESI on COM1 (before anything is set up; a
; stuck UART does no harm because of the bounded wait).
boot_puts:
    push eax
    push ecx
    push edx
.next:
    mov al, [esi]
    test al, al
    jz .done
    mov ecx, 0x10000
    mov dx, SERIAL + 5
.wait:
    in al, dx
    test al, 0x20
    jnz .ready
    loop .wait
.ready:
    mov al, [esi]
    mov dx, SERIAL
    out dx, al
    inc esi
    jmp .next
.done:
    pop edx
    pop ecx
    pop eax
    ret

_start:
    cli
    mov [boot_magic], eax
    mov [boot_info], ebx
    mov esp, boot_stack_top

    ; CPUID must exist: EFLAGS.ID (bit 21) can be flipped (Intel SDM Vol.2A CPUID).
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 0x200000
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    jz .no_long_mode

    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode

    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29               ; LM: long mode
    jz .no_long_mode

    ; Clear the tables (the loader zeroes .bss, but do not count on it).
    mov edi, boot_tables
    mov ecx, (boot_tables_end - boot_tables) / 4
    xor eax, eax
    rep stosd

    ; PML4: [0] identity, [256] direct map, [511] kernel image.
    mov eax, pdpt_low
    or eax, PAGE_PRESENT_WRITE
    mov [pml4], eax
    mov eax, pdpt_direct
    or eax, PAGE_PRESENT_WRITE
    mov [pml4 + 256 * 8], eax
    mov eax, pdpt_kernel
    or eax, PAGE_PRESENT_WRITE
    mov [pml4 + 511 * 8], eax

    ; PDPT: four page directories of 512 x 2 MiB = 4 GiB. The first one also holds the kernel image
    ; (PDPT entry 510 of the last PML4 entry = 0xFFFFFFFF80000000).
    mov eax, pd
    or eax, PAGE_PRESENT_WRITE
    mov [pdpt_low], eax
    mov [pdpt_kernel + 510 * 8], eax
    mov ecx, 4
    mov edi, pdpt_direct
    mov edx, pd
.pdpt:
    mov eax, edx
    or eax, PAGE_PRESENT_WRITE
    mov [edi], eax
    add edi, 8
    add edx, 4096
    loop .pdpt

    ; The page directories: entry n maps the 2 MiB page at n * 2 MiB.
    mov edi, pd
    mov eax, PAGE_PRESENT_WRITE | PAGE_LARGE
    mov ecx, 4 * 512
.fill:
    mov [edi], eax
    add edi, 8
    add eax, 0x200000
    loop .fill

    mov eax, cr4
    or eax, CR4_PAE
    mov cr4, eax

    mov eax, pml4
    mov cr3, eax

    mov ecx, MSR_EFER
    rdmsr
    or eax, EFER_LME | EFER_NXE
    wrmsr

    mov eax, cr0
    or eax, CR0_PE_PG_WP
    mov cr0, eax

    lgdt [boot_gdt_descriptor]
    jmp 0x08:long_mode

.no_long_mode:
    mov esi, no_long_mode_text
    call boot_puts
.hang:
    cli
    hlt
    jmp .hang

bits 64
default abs                         ; boot_magic and boot_info are below 4 GiB: plain 32-bit addresses
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov edi, [boot_magic]
    mov esi, [boot_info]

    mov rsp, kernel_stack_top
    xor ebp, ebp

    mov rax, pc_entry
    call rax

.hang:
    cli
    hlt
    jmp .hang

section .boot.data progbits alloc noexec write align=8

; 64-bit code segment (L = 1) and data segment: only what the jump into long mode needs. The kernel
; loads its own GDT as soon as it runs (arch/x86_64/gdt).
align 8
boot_gdt:
    dq 0
    dq 0x00209A0000000000           ; 0x08: code, present, DPL 0, execute/read, L = 1
    dq 0x0000920000000000           ; 0x10: data, present, DPL 0, read/write
boot_gdt_descriptor:
    dw boot_gdt_descriptor - boot_gdt - 1
    dd boot_gdt

boot_magic: dd 0
boot_info: dd 0

no_long_mode_text: db "68k_HAL: this processor has no 64-bit mode", 13, 10, 0

section .boot.bss nobits alloc noexec write align=4096

align 4096
boot_tables:
pml4:        resb 4096
pdpt_low:    resb 4096
pdpt_direct: resb 4096
pdpt_kernel: resb 4096
pd:          resb 4 * 4096
boot_tables_end:

align 16
boot_stack:  resb 4096
boot_stack_top:

section .bss
align 16
kernel_stack: resb 32768
global kernel_stack_top
kernel_stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
