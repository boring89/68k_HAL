/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>
#include <stdlib.h>

#include "kernel/display/fb_pattern.h"

#define WIDTH 64
#define HEIGHT 16
#define PITCH 288 /* 256 bytes of pixels + padding that must stay untouched */

static uint32_t pixel(const uint8_t *base, uint32_t x, uint32_t y)
{
    return *(const uint32_t *)(base + y * PITCH + x * 4);
}

static void test_pattern(void)
{
    uint8_t *buffer = malloc(PITCH * HEIGHT);
    hal_framebuffer_t fb = {buffer, WIDTH, HEIGHT, PITCH, 32};

    memset(buffer, 0xAA, PITCH * HEIGHT);
    fb_draw_test_pattern(&fb);

    /* First and last bar, and the last pixel of the first bar (64 / 8 = 8). */
    TEST_ASSERT_EQ_INT(0x00FFFFFF, pixel(buffer, 0, 0));
    TEST_ASSERT_EQ_INT(0x00FFFFFF, pixel(buffer, 7, 0));
    TEST_ASSERT_EQ_INT(0x00FFFF00, pixel(buffer, 8, 0));
    TEST_ASSERT_EQ_INT(0x00FF0000, pixel(buffer, 40, 11));
    TEST_ASSERT_EQ_INT(0x00000000, pixel(buffer, WIDTH - 1, 0));

    /* Gradient rows start at 12 (16 / 4 * 3). */
    TEST_ASSERT_EQ_INT(0x00000000, pixel(buffer, 0, 12));
    TEST_ASSERT_EQ_INT(0x00FFFFFF, pixel(buffer, WIDTH - 1, 15));

    /* Padding after the last pixel of each row is left alone. */
    TEST_ASSERT_EQ_INT(0xAAAAAAAA, pixel(buffer, WIDTH, 0));
    TEST_ASSERT_EQ_INT(0xAAAAAAAA, pixel(buffer, WIDTH, 15));

    free(buffer);
}

static void test_rejects(void)
{
    hal_framebuffer_t fb = {NULL, WIDTH, HEIGHT, PITCH, 24};
    uint8_t buffer[16] = {0};

    fb_draw_test_pattern(NULL);
    fb_draw_test_pattern(&fb);   /* NULL base, wrong depth: must not crash */
    fb.base = buffer;
    fb_draw_test_pattern(&fb);   /* 24 bpp: nothing written */
    TEST_ASSERT_EQ_INT(0, buffer[0]);
}

int main(void)
{
    TEST_RUN(test_pattern);
    TEST_RUN(test_rejects);
    return TEST_REPORT("fb_pattern");
}
