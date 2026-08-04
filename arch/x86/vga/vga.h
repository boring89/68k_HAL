/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_VGA_VGA_H
#define ARCH_X86_VGA_VGA_H

#include <stdint.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

/*
 * VGA hardware interface
 */

void vga_init(void);

void vga_clear(void);

void vga_put_cell(
    int x,
    int y,
    char c,
    uint8_t color
);

void vga_get_cell(
    int x,
    int y,
    char *c,
    unsigned char *color
);

void vga_copy_cell(
    int sx,
    int sy,
    int dx,
    int dy
);

void vga_set_cursor(
    int x,
    int y
);


#endif