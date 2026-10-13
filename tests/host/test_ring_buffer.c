/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/lib/ring_buffer.h"

#define CAPACITY 4

static int storage[CAPACITY];
static ring_buffer_t buffer;

static void reset(void)
{
    ring_buffer_init(&buffer, storage, sizeof(int), CAPACITY);
}

static void test_empty(void)
{
    int value;

    reset();

    TEST_ASSERT(!ring_buffer_has_data(&buffer));
    TEST_ASSERT(!ring_buffer_is_full(&buffer));
    TEST_ASSERT_EQ_INT(-1, ring_buffer_pop(&buffer, &value));
}

static void test_fifo_order(void)
{
    int value;

    reset();

    for (int i = 1; i <= 3; i++)
        TEST_ASSERT_EQ_INT(0, ring_buffer_push(&buffer, &i));

    for (int i = 1; i <= 3; i++)
    {
        TEST_ASSERT_EQ_INT(0, ring_buffer_pop(&buffer, &value));
        TEST_ASSERT_EQ_INT(i, value);
    }

    TEST_ASSERT(!ring_buffer_has_data(&buffer));
}

static void test_full(void)
{
    int value = 0;

    reset();

    /* One slot is kept empty to tell full from empty. */
    for (int i = 0; i < CAPACITY - 1; i++)
        TEST_ASSERT_EQ_INT(0, ring_buffer_push(&buffer, &i));

    TEST_ASSERT(ring_buffer_is_full(&buffer));
    TEST_ASSERT_EQ_INT(-1, ring_buffer_push(&buffer, &value));
}

static void test_wrap_around(void)
{
    int value;

    reset();

    for (int round = 0; round < 10; round++)
    {
        int a = round * 2;
        int b = round * 2 + 1;

        TEST_ASSERT_EQ_INT(0, ring_buffer_push(&buffer, &a));
        TEST_ASSERT_EQ_INT(0, ring_buffer_push(&buffer, &b));

        TEST_ASSERT_EQ_INT(0, ring_buffer_pop(&buffer, &value));
        TEST_ASSERT_EQ_INT(a, value);

        TEST_ASSERT_EQ_INT(0, ring_buffer_pop(&buffer, &value));
        TEST_ASSERT_EQ_INT(b, value);
    }

    TEST_ASSERT(!ring_buffer_has_data(&buffer));
}

int main(void)
{
    TEST_RUN(test_empty);
    TEST_RUN(test_fifo_order);
    TEST_RUN(test_full);
    TEST_RUN(test_wrap_around);

    return TEST_REPORT("ring_buffer");
}
