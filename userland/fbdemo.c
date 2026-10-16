/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Interactive, graphics mode only: draws on /dev/fb0 (a dark blue screen, a
 * red and a green square, a white frame) and waits for the Q key.
 */

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static uint32_t *pixels;
static uint32_t width;
static uint32_t height;
static uint32_t stride; /* pixels per row */

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    for (uint32_t row = y; row < y + h && row < height; row++)
    {
        for (uint32_t col = x; col < x + w && col < width; col++)
            pixels[row * stride + col] = color;
    }
}

int main(void)
{
    int fd = open("/dev/fb0", O_RDWR);

    if (fd < 0)
    {
        printf("fbdemo: no framebuffer (boot the graphics entry)\n");

        return 1;
    }

    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;

    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0 ||
        var.bits_per_pixel != 32)
    {
        printf("fbdemo: unsupported framebuffer\n");

        return 1;
    }

    width = var.xres;
    height = var.yres;
    stride = fix.line_length / 4;
    pixels = mmap(NULL, fix.line_length * height, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (pixels == MAP_FAILED)
    {
        printf("fbdemo: mmap failed\n");

        return 1;
    }

    printf("fbdemo: %ux%u, line %u bytes, red at bit %u\n", width, height, fix.line_length,
           var.red.offset);
    fflush(stdout);

    fill(0, 0, width, height, 0x000040);
    fill(0, 0, width, 2, 0xFFFFFF);
    fill(0, height - 2, width, 2, 0xFFFFFF);
    fill(0, 0, 2, height, 0xFFFFFF);
    fill(width - 2, 0, 2, height, 0xFFFFFF);
    fill(100, 100, 64, 64, 0xFF0000);
    fill(200, 100, 64, 64, 0x00FF00);

    int events = open("/dev/input/event0", O_RDONLY);

    if (events < 0)
    {
        printf("fbdemo: no input device\n");

        return 1;
    }

    for (;;)
    {
        struct input_event event;

        if (read(events, &event, sizeof(event)) != (ssize_t)sizeof(event))
            break;

        if (event.type == EV_KEY && event.code == KEY_Q && event.value == 1)
            break;
    }

    munmap(pixels, fix.line_length * height);
    printf("fbdemo: done\n");

    return 0;
}
