/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/shell/parser.h"

static void test_empty_input(void)
{
    char input[] = "";
    shell_command_t cmd;

    shell_parse(input, &cmd);

    TEST_ASSERT_EQ_INT(0, cmd.argc);
}

static void test_only_spaces(void)
{
    char input[] = "    ";
    shell_command_t cmd;

    shell_parse(input, &cmd);

    TEST_ASSERT_EQ_INT(0, cmd.argc);
}

static void test_arguments(void)
{
    char input[] = "echo hello world";
    shell_command_t cmd;

    shell_parse(input, &cmd);

    TEST_ASSERT_EQ_INT(3, cmd.argc);
    TEST_ASSERT_EQ_STR("echo", cmd.argv[0]);
    TEST_ASSERT_EQ_STR("hello", cmd.argv[1]);
    TEST_ASSERT_EQ_STR("world", cmd.argv[2]);
    TEST_ASSERT(cmd.argv[3] == 0);
}

static void test_extra_spaces(void)
{
    char input[] = "  alloc   64  ";
    shell_command_t cmd;

    shell_parse(input, &cmd);

    TEST_ASSERT_EQ_INT(2, cmd.argc);
    TEST_ASSERT_EQ_STR("alloc", cmd.argv[0]);
    TEST_ASSERT_EQ_STR("64", cmd.argv[1]);
}

static void test_max_args(void)
{
    char input[128];
    shell_command_t cmd;
    int pos = 0;

    for (int i = 0; i < SHELL_MAX_ARGS + 4; i++)
        pos += snprintf(input + pos, sizeof(input) - pos, "a ");

    shell_parse(input, &cmd);

    TEST_ASSERT_EQ_INT(SHELL_MAX_ARGS, cmd.argc);
}

int main(void)
{
    TEST_RUN(test_empty_input);
    TEST_RUN(test_only_spaces);
    TEST_RUN(test_arguments);
    TEST_RUN(test_extra_spaces);
    TEST_RUN(test_max_args);

    return TEST_REPORT("parser");
}
