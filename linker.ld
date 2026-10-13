/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

ENTRY(_start)

SECTIONS
{
    . = 1M;

    _kernel_start = .;

    .text :
    {
        KEEP(*(.multiboot))
        *(.text)
    }

    .rodata :
    {
        *(.rodata*)
    }

    .data :
    {
        *(.data)
    }

    .bss :
    {
        *(.bss)
        *(COMMON)
    }

    _kernel_end = .;
}