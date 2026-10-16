/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/user/stack.h"

#include <string.h>

#define TOP 0xC0000000u
#define CAP 4096

static uint32_t word(const uint8_t *buf, uint32_t sp, uint32_t addr)
{
    const uint8_t *p = buf + CAP - (TOP - addr);
    (void)sp;

    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static const char *str(const uint8_t *buf, uint32_t addr)
{
    return (const char *)(buf + CAP - (TOP - addr));
}

static void test_layout(void)
{
    uint8_t buf[CAP];
    const char *argv[] = {"prog", "-x", "file", NULL};
    const char *envp[] = {"HOME=/", "TERM=vt100", NULL};
    user_stack_params_t params = {0x08048034, 32, 6, 0x08049059, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
    uint32_t sp = 0;

    memset(buf, 0xAA, sizeof(buf));
    size_t used = user_stack_build(buf, CAP, TOP, argv, envp, &params, &sp);

    TEST_ASSERT(used > 0);
    TEST_ASSERT_EQ_INT(0, sp & 15);
    TEST_ASSERT_EQ_INT(TOP - used, sp);

    TEST_ASSERT_EQ_INT(3, word(buf, sp, sp));
    TEST_ASSERT_EQ_STR("prog", str(buf, word(buf, sp, sp + 4)));
    TEST_ASSERT_EQ_STR("-x", str(buf, word(buf, sp, sp + 8)));
    TEST_ASSERT_EQ_STR("file", str(buf, word(buf, sp, sp + 12)));
    TEST_ASSERT_EQ_INT(0, word(buf, sp, sp + 16));
    TEST_ASSERT_EQ_STR("HOME=/", str(buf, word(buf, sp, sp + 20)));
    TEST_ASSERT_EQ_STR("TERM=vt100", str(buf, word(buf, sp, sp + 24)));
    TEST_ASSERT_EQ_INT(0, word(buf, sp, sp + 28));

    /* auxv follows: find AT_RANDOM and AT_PAGESZ, end with AT_NULL. */
    uint32_t a = sp + 32;
    int found_pagesz = 0, found_random = 0, found_phnum = 0;

    for (int i = 0; i < 32; i++, a += 8)
    {
        uint32_t type = word(buf, sp, a);
        uint32_t value = word(buf, sp, a + 4);

        if (type == AT_NULL)
            break;

        if (type == AT_PAGESZ)
            found_pagesz = value == 4096;

        if (type == AT_PHNUM)
            found_phnum = value == 6;

        if (type == AT_RANDOM)
        {
            found_random = 1;
            TEST_ASSERT_EQ_INT(1, *(const uint8_t *)str(buf, value));
            TEST_ASSERT_EQ_INT(16, ((const uint8_t *)str(buf, value))[15]);
        }
    }

    TEST_ASSERT(found_pagesz);
    TEST_ASSERT(found_random);
    TEST_ASSERT(found_phnum);
    TEST_ASSERT_EQ_INT(0, word(buf, sp, a + 4));
}

static void test_no_arguments(void)
{
    uint8_t buf[CAP];
    user_stack_params_t params = {0};
    uint32_t sp = 0;

    TEST_ASSERT(user_stack_build(buf, CAP, TOP, NULL, NULL, &params, &sp) > 0);
    TEST_ASSERT_EQ_INT(0, sp & 15);
    TEST_ASSERT_EQ_INT(0, word(buf, sp, sp));
    TEST_ASSERT_EQ_INT(0, word(buf, sp, sp + 4)); /* argv NULL */
    TEST_ASSERT_EQ_INT(0, word(buf, sp, sp + 8)); /* envp NULL */
}

static void test_alignment_varies(void)
{
    /* Strings of every length must still give a 16-byte aligned sp. */
    for (int n = 1; n < 40; n++)
    {
        uint8_t buf[CAP];
        char arg[64];
        const char *argv[] = {arg, NULL};
        user_stack_params_t params = {0};
        uint32_t sp = 0;

        memset(arg, 'x', (size_t)n);
        arg[n] = '\0';
        TEST_ASSERT(user_stack_build(buf, CAP, TOP, argv, NULL, &params, &sp) > 0);
        TEST_ASSERT_EQ_INT(0, sp & 15);
        TEST_ASSERT_EQ_STR(arg, str(buf, word(buf, sp, sp + 4)));
    }
}

static void test_too_big(void)
{
    uint8_t buf[64];
    const char *argv[] = {"a-fairly-long-argument-that-cannot-fit-in-sixty-four-bytes-at-all", NULL};
    user_stack_params_t params = {0};
    uint32_t sp = 0;

    TEST_ASSERT_EQ_INT(0, (int)user_stack_build(buf, sizeof(buf), TOP, argv, NULL, &params, &sp));
}

int main(void)
{
    TEST_RUN(test_layout);
    TEST_RUN(test_no_arguments);
    TEST_RUN(test_alignment_varies);
    TEST_RUN(test_too_big);

    return TEST_REPORT("user_stack");
}
