/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/display/fb_scale.h"

#include <string.h>

static void test_double(void)
{
    const uint32_t source[2 * 2] = {1, 2, 3, 4};
    uint32_t screen[6 * 5]; /* the target is 4x4 inside a 6x5 frame buffer, at (1,1) */

    memset(screen, 0xFF, sizeof(screen));
    fb_blit_scaled((uint8_t *)(screen + 6 + 1), 6 * 4, source, 2, 2, 2);

    const uint32_t row0[4] = {1, 1, 2, 2};
    const uint32_t row2[4] = {3, 3, 4, 4};

    TEST_ASSERT(memcmp(screen + 6 + 1, row0, 16) == 0);
    TEST_ASSERT(memcmp(screen + 2 * 6 + 1, row0, 16) == 0);
    TEST_ASSERT(memcmp(screen + 3 * 6 + 1, row2, 16) == 0);
    TEST_ASSERT(memcmp(screen + 4 * 6 + 1, row2, 16) == 0);

    /* Nothing outside the target area changed. */
    TEST_ASSERT_EQ_INT((int)0xFFFFFFFFu, (int)screen[0]);
    TEST_ASSERT_EQ_INT((int)0xFFFFFFFFu, (int)screen[6]);
    TEST_ASSERT_EQ_INT((int)0xFFFFFFFFu, (int)screen[6 + 5]);
    TEST_ASSERT_EQ_INT((int)0xFFFFFFFFu, (int)screen[5 * 6 - 1]);
}

static void test_scale_one_and_three(void)
{
    const uint32_t source[3] = {7, 8, 9};
    uint32_t out1[3];
    uint32_t out3[9 * 3];

    fb_blit_scaled((uint8_t *)out1, 12, source, 3, 1, 1);
    TEST_ASSERT(memcmp(out1, source, 12) == 0);

    fb_blit_scaled((uint8_t *)out3, 36, source, 3, 1, 3);

    for (int row = 0; row < 3; row++)
    {
        for (int x = 0; x < 9; x++)
            TEST_ASSERT_EQ_INT(7 + x / 3, out3[row * 9 + x]);
    }

    uint32_t untouched = 5;

    fb_blit_scaled((uint8_t *)&untouched, 4, source, 1, 1, 0);
    TEST_ASSERT_EQ_INT(5, untouched);
}

static void test_changed_rows_only(void)
{
    const uint32_t first[2 * 3] = {1, 2, 3, 4, 5, 6};
    uint32_t second[2 * 3] = {1, 2, 9, 9, 5, 6}; /* only the middle row changed */
    uint32_t screen[4 * 6];

    memset(screen, 0, sizeof(screen));

    /* No previous picture: everything is drawn. */
    TEST_ASSERT_EQ_INT(3, fb_blit_scaled_rows((uint8_t *)screen, 16, first, NULL, 2, 3, 2));
    TEST_ASSERT_EQ_INT(1, screen[0]);
    TEST_ASSERT_EQ_INT(6, screen[5 * 4 + 3]);

    /* A marker in the rows that must stay: it is not overwritten by an identical row. */
    screen[0] = 77;
    screen[5 * 4] = 88;

    TEST_ASSERT_EQ_INT(1, fb_blit_scaled_rows((uint8_t *)screen, 16, second, first, 2, 3, 2));
    TEST_ASSERT_EQ_INT(77, screen[0]);
    TEST_ASSERT_EQ_INT(88, screen[5 * 4]);
    /* Row 1 (output rows 2 and 3) was redrawn, enlarged. */
    TEST_ASSERT_EQ_INT(9, screen[2 * 4]);
    TEST_ASSERT_EQ_INT(9, screen[2 * 4 + 3]);
    TEST_ASSERT_EQ_INT(9, screen[3 * 4]);

    /* The same picture again: nothing to do. */
    TEST_ASSERT_EQ_INT(0, fb_blit_scaled_rows((uint8_t *)screen, 16, second, second, 2, 3, 2));
    TEST_ASSERT_EQ_INT(0, fb_blit_scaled_rows((uint8_t *)screen, 16, second, second, 2, 3, 0));
}

static void test_best_scale(void)
{
    TEST_ASSERT_EQ_INT(1, fb_best_scale(640, 480, 640, 480));
    TEST_ASSERT_EQ_INT(1, fb_best_scale(640, 480, 800, 600));
    TEST_ASSERT_EQ_INT(2, fb_best_scale(640, 480, 1280, 960));
    TEST_ASSERT_EQ_INT(2, fb_best_scale(640, 480, 1920, 1080)); /* the height limits it */
    TEST_ASSERT_EQ_INT(3, fb_best_scale(640, 480, 1920, 1440));
    TEST_ASSERT_EQ_INT(1, fb_best_scale(640, 480, 320, 200));
    TEST_ASSERT_EQ_INT(1, fb_best_scale(0, 0, 100, 100));
}

static void test_halves(void)
{
    const uint32_t source[2 * 2] = {1, 2, 3, 4};
    uint32_t screen[3 * 3];
    uint32_t row[3];
    const uint8_t only_second[2] = {0, 1};

    /* 1.5x: 2x2 becomes 3x3; source pixel 0 covers destination pixels 0 and 1. */
    memset(screen, 0, sizeof(screen));
    TEST_ASSERT_EQ_INT(2, (int)fb_blit_halves((uint8_t *)screen, 3 * 4, source, NULL, 2, 2, 3, row));
    const uint32_t want[9] = {1, 1, 2, 1, 1, 2, 3, 3, 4};
    TEST_ASSERT(memcmp(screen, want, sizeof(want)) == 0);

    /* Only the changed row is drawn: row 1 of the source is destination row 2 at 1.5x. */
    memset(screen, 0, sizeof(screen));
    TEST_ASSERT_EQ_INT(1, (int)fb_blit_halves((uint8_t *)screen, 3 * 4, source, only_second, 2, 2, 3, row));
    TEST_ASSERT_EQ_INT(0, (int)screen[0]);
    TEST_ASSERT_EQ_INT(0, (int)screen[3]);
    TEST_ASSERT_EQ_INT(3, (int)screen[6]);
    TEST_ASSERT_EQ_INT(4, (int)screen[8]);

    /* 2x through the same function. */
    uint32_t big[4 * 4];
    uint32_t row4[4];
    TEST_ASSERT_EQ_INT(2, (int)fb_blit_halves((uint8_t *)big, 4 * 4, source, NULL, 2, 2, 4, row4));
    TEST_ASSERT_EQ_INT(2, (int)big[3]);
    TEST_ASSERT_EQ_INT(3, (int)big[3 * 4]);
    TEST_ASSERT_EQ_INT(0, (int)fb_blit_halves((uint8_t *)big, 16, source, NULL, 2, 2, 0, row4));

    /* 0.5x: every second pixel of every second row (2x2 -> 1x1: pixel (0,0)). */
    uint32_t small[1] = {0};
    TEST_ASSERT_EQ_INT(1, (int)fb_blit_halves((uint8_t *)small, 4, source, NULL, 2, 2, 1, row4));
    TEST_ASSERT_EQ_INT(1, (int)small[0]);
    TEST_ASSERT_EQ_INT(960, (int)fb_scaled_size(640, 3));
}

static void test_halves_step(void)
{
    TEST_ASSERT_EQ_INT(5, (int)fb_halves_step(4, 1));
    TEST_ASSERT_EQ_INT(3, (int)fb_halves_step(4, -1));
    TEST_ASSERT_EQ_INT(1, (int)fb_halves_step(2, -1));
    TEST_ASSERT_EQ_INT(1, (int)fb_halves_step(1, -1));
    TEST_ASSERT_EQ_INT(FB_SCALE_HALVES_MAX, (int)fb_halves_step(FB_SCALE_HALVES_MAX, 1));
}

static void test_choose_size(void)
{
    const uint16_t w[] = {640, 800, 1024, 1280, 1920, 2560};
    const uint16_t h[] = {480, 600, 768, 720, 1080, 1440};
    uint32_t index, halves;

    /* 2560x1440: 1280x720 at 2x fills it (a tie with 2560x1440 at 1x; the larger enlargement wins). */
    TEST_ASSERT_EQ_INT(0, fb_choose_size(w, h, 6, 2560, 1440, &index, &halves));
    TEST_ASSERT_EQ_INT(3, (int)index);
    TEST_ASSERT_EQ_INT(4, (int)halves);

    /* 1920x1080: 1280x720 at 1.5x. */
    fb_choose_size(w, h, 6, 1920, 1080, &index, &halves);
    TEST_ASSERT_EQ_INT(3, (int)index);
    TEST_ASSERT_EQ_INT(3, (int)halves);

    /* 640x480: itself at 1x. */
    fb_choose_size(w, h, 6, 640, 480, &index, &halves);
    TEST_ASSERT_EQ_INT(0, (int)index);
    TEST_ASSERT_EQ_INT(2, (int)halves);

    /* Too small for anything. */
    TEST_ASSERT_EQ_INT(-1, fb_choose_size(w, h, 6, 320, 200, &index, &halves));
    TEST_ASSERT_EQ_INT(0, (int)index);
}

int main(void)
{
    TEST_RUN(test_double);
    TEST_RUN(test_scale_one_and_three);
    TEST_RUN(test_changed_rows_only);
    TEST_RUN(test_best_scale);
    TEST_RUN(test_halves);
    TEST_RUN(test_choose_size);
    TEST_RUN(test_halves_step);

    return TEST_REPORT("fb_scale");
}
