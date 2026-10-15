/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

ENTRY(_start)

/*
 * One LOAD segment per permission set, so none of them is RWX.
 * p_flags bits: PF_X = 1, PF_W = 2, PF_R = 4 (System V ABI, ELF).
 */
PHDRS
{
    text   PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data   PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 1M;

    _kernel_start = .;

    /*
     * The Multiboot header must be within the first 8 KiB of the file
     * (Multiboot specification 3.1), so it goes first in .text.
     */
    .text ALIGN(4K) :
    {
        KEEP(*(.multiboot))
        *(.text .text.*)
    } :text

    .rodata ALIGN(4K) :
    {
        *(.rodata .rodata.*)
    } :rodata

    .data ALIGN(4K) :
    {
        *(.data .data.*)
    } :data

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    . = ALIGN(4K);
    _kernel_end = .;

    /* Pages paging_init maps read-only: .text and .rodata. */
    _kernel_ro_start = ADDR(.text);
    _kernel_ro_end = ADDR(.data);
}
