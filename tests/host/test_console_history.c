/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/console/history.h"

#include <string.h>

static void test_lines(void)
{
    char buffer[256];

    console_history_clear();
    console_history_add("one\ntwo\nthree\n");

    TEST_ASSERT_EQ_INT(14, console_history_size());
    console_history_last_lines(buffer, sizeof(buffer), 0);
    TEST_ASSERT(strcmp(buffer, "one\ntwo\nthree\n") == 0);
    console_history_last_lines(buffer, sizeof(buffer), 2);
    TEST_ASSERT(strcmp(buffer, "two\nthree\n") == 0);
    console_history_last_lines(buffer, sizeof(buffer), 1);
    TEST_ASSERT(strcmp(buffer, "three\n") == 0);
    console_history_last_lines(buffer, sizeof(buffer), 10);
    TEST_ASSERT(strcmp(buffer, "one\ntwo\nthree\n") == 0);

    /* Without a final newline the unfinished line counts. */
    console_history_add("four");
    console_history_last_lines(buffer, sizeof(buffer), 2);
    TEST_ASSERT(strcmp(buffer, "three\nfour") == 0);
}

static void test_small_buffer_keeps_the_end(void)
{
    char buffer[8];

    console_history_clear();
    console_history_add("abcdefghij");
    TEST_ASSERT_EQ_INT(7, console_history_last_lines(buffer, sizeof(buffer), 0));
    TEST_ASSERT(strcmp(buffer, "defghij") == 0);
    TEST_ASSERT_EQ_INT(0, console_history_last_lines(buffer, 0, 0));
}

static void test_wraps_around(void)
{
    static char big[CONSOLE_HISTORY_SIZE + 100];
    char buffer[64];

    console_history_clear();

    for (size_t i = 0; i < sizeof(big) - 1; i++)
        big[i] = (char)('a' + i % 26);

    big[sizeof(big) - 1] = '\0';
    console_history_add(big);
    console_history_add("\nlast line\n");

    TEST_ASSERT_EQ_INT(CONSOLE_HISTORY_SIZE, console_history_size());
    console_history_last_lines(buffer, sizeof(buffer), 1);
    TEST_ASSERT(strcmp(buffer, "last line\n") == 0);
}

int main(void)
{
    TEST_RUN(test_lines);
    TEST_RUN(test_small_buffer_keeps_the_end);
    TEST_RUN(test_wraps_around);

    return TEST_REPORT("console_history");
}
