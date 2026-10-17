/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * The 64-bit kernel (ARCH=x86_64, docs/x86_64.md). The boot code (.boot.*: 32-bit, then the jump
 * to long mode) is linked at its physical address, 1 MiB. The kernel proper is linked in the
 * top 2 GiB of the address space (the "kernel" code model of the x86-64 ABI) and loaded right
 * behind the boot code: virtual = physical + KERNEL_OFFSET.
 */

ENTRY(_start)

KERNEL_OFFSET = 0xFFFFFFFF80000000;

PHDRS
{
    boottext PT_LOAD FLAGS(5);
    bootdata PT_LOAD FLAGS(6);
    text   PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data   PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 1M;

    _kernel_start = .;

    /* The Multiboot headers must be within the first 8 KiB of the file (Multiboot specification 3.1). */
    .boot.text ALIGN(4K) :
    {
        KEEP(*(.multiboot))
        *(.boot.text)
    } :boottext

    .boot.data ALIGN(4K) :
    {
        *(.boot.data)
    } :bootdata

    /* Page tables and the stack of the boot code. Zero-filled by the loader (and again by the code). */
    .boot.bss ALIGN(4K) :
    {
        *(.boot.bss)
    } :bootdata

    . = ALIGN(4K);
    . += KERNEL_OFFSET;

    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_OFFSET)
    {
        *(.text .text.*)
    } :text

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_OFFSET)
    {
        *(.rodata .rodata.*)
    } :rodata

    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_OFFSET)
    {
        *(.data .data.*)
    } :data

    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_OFFSET)
    {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    . = ALIGN(4K);
    _kernel_end = . - KERNEL_OFFSET;

    /* Pages paging_init maps read-only: .text (executable) and .rodata (virtual addresses). */
    _kernel_ro_start = ADDR(.text);
    _kernel_text_end = ADDR(.rodata);
    _kernel_ro_end = ADDR(.data);

    /DISCARD/ : { *(.note.GNU-stack) *(.eh_frame) *(.comment) }
}
