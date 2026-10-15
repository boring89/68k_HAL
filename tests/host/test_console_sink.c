/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stddef.h>
#include <string.h>

#include "kernel/console/sink.h"

/* Two recording sinks; each appends what it receives to its buffer. */
static char out_a[64];
static char out_b[64];

static void append(char *buf, const char *str)
{
    strncat(buf, str, 63 - strlen(buf));
}

static void append_char(char *buf, char c)
{
    char str[2] = {c, 0};

    append(buf, str);
}

static void a_write(const char *str)
{
    append(out_a, str);
}

static void a_write_char(char c)
{
    append_char(out_a, c);
}

static void b_write(const char *str)
{
    append(out_b, str);
}

static void b_write_char(char c)
{
    append_char(out_b, c);
}

static const console_sink_t sink_a = {a_write, a_write_char};
static const console_sink_t sink_b = {b_write, b_write_char};

static void reset(void)
{
    console_sinks_reset();
    out_a[0] = 0;
    out_b[0] = 0;
}

static void test_no_sinks(void)
{
    reset();

    /* Nothing registered: writing must not crash. */
    console_sinks_write("x");
    console_sinks_write_char('y');

    TEST_ASSERT_EQ_STR("", out_a);
}

static void test_broadcast(void)
{
    reset();

    TEST_ASSERT_EQ_INT(0, console_register_sink(&sink_a));
    TEST_ASSERT_EQ_INT(0, console_register_sink(&sink_b));

    console_sinks_write("ab");
    console_sinks_write_char('c');

    TEST_ASSERT_EQ_STR("abc", out_a);
    TEST_ASSERT_EQ_STR("abc", out_b);
}

static void test_rejects_null(void)
{
    static const console_sink_t no_write = {NULL, a_write_char};
    static const console_sink_t no_char = {a_write, NULL};

    reset();

    TEST_ASSERT_EQ_INT(-1, console_register_sink(NULL));
    TEST_ASSERT_EQ_INT(-1, console_register_sink(&no_write));
    TEST_ASSERT_EQ_INT(-1, console_register_sink(&no_char));
}

static void test_rejects_duplicate(void)
{
    reset();

    TEST_ASSERT_EQ_INT(0, console_register_sink(&sink_a));
    TEST_ASSERT_EQ_INT(-1, console_register_sink(&sink_a));

    /* Registered once, so written once. */
    console_sinks_write("z");
    TEST_ASSERT_EQ_STR("z", out_a);
}

static void test_rejects_when_full(void)
{
    static console_sink_t extra[CONSOLE_MAX_SINKS];

    reset();

    for (int i = 0; i < CONSOLE_MAX_SINKS; i++)
    {
        extra[i].write = b_write;
        extra[i].write_char = b_write_char;
        TEST_ASSERT_EQ_INT(0, console_register_sink(&extra[i]));
    }

    TEST_ASSERT_EQ_INT(-1, console_register_sink(&sink_a));

    console_sinks_write("q");
    TEST_ASSERT_EQ_STR("", out_a);
}

static void test_reset_forgets(void)
{
    reset();

    TEST_ASSERT_EQ_INT(0, console_register_sink(&sink_a));
    console_sinks_reset();

    console_sinks_write("w");
    TEST_ASSERT_EQ_STR("", out_a);
}

int main(void)
{
    TEST_RUN(test_no_sinks);
    TEST_RUN(test_broadcast);
    TEST_RUN(test_rejects_null);
    TEST_RUN(test_rejects_duplicate);
    TEST_RUN(test_rejects_when_full);
    TEST_RUN(test_reset_forgets);

    return TEST_REPORT("console_sink");
}
