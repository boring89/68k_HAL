/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/lib/k_string.h"
#include "kernel/lib/k_memory.h"

static void test_strlen(void)
{
    TEST_ASSERT_EQ_INT(0, k_strlen(""));
    TEST_ASSERT_EQ_INT(5, k_strlen("hello"));
}

static void test_strcmp(void)
{
    TEST_ASSERT_EQ_INT(0, k_strcmp("abc", "abc"));
    TEST_ASSERT_EQ_INT(0, k_strcmp("", ""));
    TEST_ASSERT(k_strcmp("abc", "abd") < 0);
    TEST_ASSERT(k_strcmp("abd", "abc") > 0);
    TEST_ASSERT(k_strcmp("ab", "abc") < 0);
    TEST_ASSERT(k_strcmp("abc", "ab") > 0);
}

static void test_strcpy(void)
{
    char buffer[16];

    k_strcpy(buffer, "kernel");
    TEST_ASSERT_EQ_STR("kernel", buffer);

    k_strcpy(buffer, "");
    TEST_ASSERT_EQ_STR("", buffer);
}

static void test_memory(void)
{
    char a[8];
    char b[8];

    k_memset(a, 0xAA, sizeof(a));
    TEST_ASSERT_EQ_INT(0xAA, (unsigned char)a[0]);
    TEST_ASSERT_EQ_INT(0xAA, (unsigned char)a[7]);

    k_memcpy(b, "1234567", 8);
    TEST_ASSERT_EQ_STR("1234567", b);

    TEST_ASSERT_EQ_INT(0, k_memcmp(b, "1234567", 8));
    TEST_ASSERT(k_memcmp(b, "1234568", 8) != 0);
}

static void test_strncmp(void)
{
    TEST_ASSERT_EQ_INT(0, k_strncmp("/tmp/x", "/tmp", 4));
    TEST_ASSERT_EQ_INT(0, k_strncmp("abc", "abd", 2));
    TEST_ASSERT(k_strncmp("abc", "abd", 3) < 0);
    TEST_ASSERT_EQ_INT(0, k_strncmp("abc", "xyz", 0));
    TEST_ASSERT(k_strncmp("ab", "abc", 3) < 0);
}

static void test_strstr(void)
{
    const char *text = "quiet noapic nosmp";

    TEST_ASSERT(k_strstr(text, "noapic") == text + 6);
    TEST_ASSERT(k_strstr(text, "nosmp") == text + 13);
    TEST_ASSERT(k_strstr(text, "apic ") == text + 8);
    TEST_ASSERT(k_strstr(text, "nousb") == NULL);
    TEST_ASSERT(k_strstr(text, "") == text);
    TEST_ASSERT(k_strstr("", "x") == NULL);
    TEST_ASSERT(k_strstr("no", "noapic") == NULL);
}

int main(void)
{
    TEST_RUN(test_strlen);
    TEST_RUN(test_strcmp);
    TEST_RUN(test_strcpy);
    TEST_RUN(test_strncmp);
    TEST_RUN(test_strstr);
    TEST_RUN(test_memory);

    return TEST_REPORT("k_string");
}
