/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TESTS_HOST_TEST_H
#define TESTS_HOST_TEST_H

#include <stdio.h>
#include <string.h>

static int test_failures = 0;
static int test_checks = 0;

#define TEST_ASSERT(cond)                                       \
    do                                                          \
    {                                                           \
        test_checks++;                                          \
        if (!(cond))                                            \
        {                                                       \
            test_failures++;                                    \
            fprintf(stderr, "  %s:%d: assertion failed: %s\n",  \
                    __FILE__, __LINE__, #cond);                 \
        }                                                       \
    } while (0)

#define TEST_ASSERT_EQ_INT(expected, actual)                    \
    do                                                          \
    {                                                           \
        long long _e = (long long)(expected);                   \
        long long _a = (long long)(actual);                     \
        test_checks++;                                          \
        if (_e != _a)                                           \
        {                                                       \
            test_failures++;                                    \
            fprintf(stderr, "  %s:%d: %s: expected %lld, got %lld\n", \
                    __FILE__, __LINE__, #actual, _e, _a);       \
        }                                                       \
    } while (0)

#define TEST_ASSERT_EQ_STR(expected, actual)                    \
    do                                                          \
    {                                                           \
        const char *_e = (expected);                            \
        const char *_a = (actual);                              \
        test_checks++;                                          \
        if (strcmp(_e, _a) != 0)                                \
        {                                                       \
            test_failures++;                                    \
            fprintf(stderr, "  %s:%d: %s: expected \"%s\", got \"%s\"\n", \
                    __FILE__, __LINE__, #actual, _e, _a);       \
        }                                                       \
    } while (0)

#define TEST_RUN(fn)                                            \
    do                                                          \
    {                                                           \
        int _before = test_failures;                            \
        fn();                                                   \
        printf("  %-40s %s\n", #fn,                             \
               test_failures == _before ? "PASS" : "FAIL");     \
    } while (0)

#define TEST_REPORT(suite)                                      \
    (printf("%s: %d checks, %d failed\n",                       \
            (suite), test_checks, test_failures),               \
     test_failures == 0 ? 0 : 1)

#endif
