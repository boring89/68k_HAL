/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/lib/k_time.h"
#include "kernel/user/region.h"

static void test_add_and_contains(void)
{
    user_regions_t set;

    user_regions_clear(&set);
    TEST_ASSERT_EQ_INT(0, user_regions_add(&set, 0x1000, 0x3000));
    TEST_ASSERT_EQ_INT(-1, user_regions_add(&set, 0x2000, 0x4000));
    TEST_ASSERT_EQ_INT(-1, user_regions_add(&set, 0x5000, 0x5000));
    TEST_ASSERT_EQ_INT(0, user_regions_add(&set, 0x3000, 0x4000));
    TEST_ASSERT_EQ_INT(1, set.count); /* [0x1000,0x3000) and [0x3000,0x4000) became one */

    TEST_ASSERT(user_regions_contains(&set, 0x1000, 0x3000));
    TEST_ASSERT(user_regions_contains(&set, 0x2ff0, 0x20)); /* across two regions */
    TEST_ASSERT(!user_regions_contains(&set, 0x0fff, 2));
    TEST_ASSERT(!user_regions_contains(&set, 0x3ff0, 0x20));
    TEST_ASSERT(user_regions_contains(&set, 0x9999, 0));    /* empty range */
    TEST_ASSERT(!user_regions_contains(&set, 0xFFFFFFF0u, 0x20)); /* wraps */
}

static void test_merge(void)
{
    user_regions_t set;

    user_regions_clear(&set);

    /* A heap that grows a page at a time stays one region, whatever the order. */
    for (uint32_t page = 0; page < 300; page++)
        TEST_ASSERT_EQ_INT(0, user_regions_add(&set, 0x10000 + page * 0x1000, 0x11000 + page * 0x1000));

    TEST_ASSERT_EQ_INT(1, set.count);
    TEST_ASSERT(user_regions_contains(&set, 0x10000, 300 * 0x1000));

    user_regions_clear(&set);
    user_regions_add(&set, 0x1000, 0x2000);
    user_regions_add(&set, 0x3000, 0x4000);
    TEST_ASSERT_EQ_INT(2, set.count);
    user_regions_add(&set, 0x2000, 0x3000); /* fills the gap: all three join */
    TEST_ASSERT_EQ_INT(1, set.count);
    TEST_ASSERT(user_regions_contains(&set, 0x1000, 0x3000));
}

static void test_remove(void)
{
    user_regions_t set;

    user_regions_clear(&set);
    user_regions_add(&set, 0x1000, 0x9000);

    /* Punch a hole in the middle: splits into two. */
    TEST_ASSERT_EQ_INT(0, user_regions_remove(&set, 0x3000, 0x5000));
    TEST_ASSERT_EQ_INT(2, set.count);
    TEST_ASSERT(user_regions_contains(&set, 0x1000, 0x2000));
    TEST_ASSERT(!user_regions_contains(&set, 0x2fff, 2));
    TEST_ASSERT(!user_regions_contains(&set, 0x4000, 1));
    TEST_ASSERT(user_regions_contains(&set, 0x5000, 0x4000));

    /* Trim the front and the back. */
    TEST_ASSERT_EQ_INT(0, user_regions_remove(&set, 0x0000, 0x2000));
    TEST_ASSERT(!user_regions_contains(&set, 0x1000, 1));
    TEST_ASSERT(user_regions_contains(&set, 0x2000, 0x1000));
    TEST_ASSERT_EQ_INT(0, user_regions_remove(&set, 0x8000, 0xA000));
    TEST_ASSERT(!user_regions_contains(&set, 0x8000, 1));
    TEST_ASSERT(user_regions_contains(&set, 0x5000, 0x3000));

    /* Remove everything, including across regions. */
    TEST_ASSERT_EQ_INT(0, user_regions_remove(&set, 0, 0xFFFFF000u));
    TEST_ASSERT_EQ_INT(0, set.count);
    TEST_ASSERT_EQ_INT(0, user_regions_remove(&set, 0x1000, 0x2000)); /* nothing to do */
}

static void test_full(void)
{
    user_regions_t set;

    user_regions_clear(&set);

    for (uint32_t i = 0; i < USER_MAX_REGIONS; i++)
        TEST_ASSERT_EQ_INT(0, user_regions_add(&set, 0x10000 + i * 0x2000, 0x11000 + i * 0x2000));

    TEST_ASSERT_EQ_INT(-1, user_regions_add(&set, 0x900000, 0x901000));

    /* A split would need one more slot than there is. */
    user_regions_remove(&set, 0x10000 + 0x400, 0x10000 + 0x800);
    TEST_ASSERT_EQ_INT(USER_MAX_REGIONS, set.count);
    TEST_ASSERT(user_regions_contains(&set, 0x10000, 0x1000));
}

static void test_epoch(void)
{
    TEST_ASSERT_EQ_INT(0, (int)k_epoch_from_civil(1970, 1, 1, 0, 0, 0));
    TEST_ASSERT_EQ_INT(86399, (int)k_epoch_from_civil(1970, 1, 1, 23, 59, 59));
    TEST_ASSERT_EQ_INT(951782400, (int)k_epoch_from_civil(2000, 2, 29, 0, 0, 0));
    TEST_ASSERT_EQ_INT(1000000000, (int)k_epoch_from_civil(2001, 9, 9, 1, 46, 40));
    TEST_ASSERT(k_epoch_from_civil(2026, 10, 17, 0, 0, 0) == 1792195200);
    TEST_ASSERT(k_epoch_from_civil(1969, 12, 31, 23, 59, 59) == -1);
    /* 2100 is not a leap year, 2000 is. */
    TEST_ASSERT(k_epoch_from_civil(2100, 3, 1, 0, 0, 0) - k_epoch_from_civil(2100, 2, 28, 0, 0, 0) == 86400);
    TEST_ASSERT(k_epoch_from_civil(2000, 3, 1, 0, 0, 0) - k_epoch_from_civil(2000, 2, 28, 0, 0, 0) == 2 * 86400);
}

int main(void)
{
    TEST_RUN(test_add_and_contains);
    TEST_RUN(test_merge);
    TEST_RUN(test_remove);
    TEST_RUN(test_full);
    TEST_RUN(test_epoch);

    return TEST_REPORT("region");
}
