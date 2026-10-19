/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "emu/mac/mac_roman.h"

static void test_table(void)
{
    TEST_ASSERT_EQ_INT('A', (int)mac_roman_to_unicode('A'));
    TEST_ASSERT_EQ_INT(0xC4, (int)mac_roman_to_unicode(0x80));      /* A with diaeresis */
    TEST_ASSERT_EQ_INT(0xE9, (int)mac_roman_to_unicode(0x8E));      /* e with acute */
    TEST_ASSERT_EQ_INT(0xA4, (int)mac_roman_to_unicode(0xDB));      /* currency sign, as in System 7.5 */
    TEST_ASSERT_EQ_INT(0xF8FF, (int)mac_roman_to_unicode(0xF0));    /* Apple's private character */
    TEST_ASSERT_EQ_INT(0x2C7, (int)mac_roman_to_unicode(0xFF));     /* caron */

    /* Every byte goes to Unicode and back. */
    for (int c = 0; c < 256; c++)
        TEST_ASSERT_EQ_INT(c, mac_roman_from_unicode(mac_roman_to_unicode((uint8_t)c)));
    TEST_ASSERT_EQ_INT(-1, mac_roman_from_unicode(0x20AC));        /* euro sign */
    TEST_ASSERT_EQ_INT(-1, mac_roman_from_unicode(0x65E5));        /* a CJK character */
}

static void test_utf8(void)
{
    const uint8_t text[] = "a\xC3\xA9\xE2\x80\xA0\xF0\x9F\x98\x80\xC3";
    const uint8_t *s = text, *end = text + sizeof text - 1;

    TEST_ASSERT_EQ_INT('a', (int)mac_roman_utf8_next(&s, end));
    TEST_ASSERT_EQ_INT(0xE9, (int)mac_roman_utf8_next(&s, end));
    TEST_ASSERT_EQ_INT(0x2020, (int)mac_roman_utf8_next(&s, end));
    TEST_ASSERT_EQ_INT(0x1F600, (int)mac_roman_utf8_next(&s, end));
    TEST_ASSERT_EQ_INT(-1, (int)mac_roman_utf8_next(&s, end));     /* cut off at the end */
    TEST_ASSERT(s == end);

    const uint8_t overlong[] = "\xC0\xAF";

    s = overlong;
    TEST_ASSERT_EQ_INT(-1, (int)mac_roman_utf8_next(&s, overlong + 2));

    uint8_t out[3];

    TEST_ASSERT_EQ_INT(1, (int)mac_roman_to_utf8('z', out));
    TEST_ASSERT_EQ_INT('z', out[0]);
    TEST_ASSERT_EQ_INT(2, (int)mac_roman_to_utf8(0x8E, out));
    TEST_ASSERT(out[0] == 0xC3 && out[1] == 0xA9);
    TEST_ASSERT_EQ_INT(3, (int)mac_roman_to_utf8(0xA0, out));      /* dagger U+2020 */
    TEST_ASSERT(out[0] == 0xE2 && out[1] == 0x80 && out[2] == 0xA0);

    /* Every byte back from its UTF-8. */
    for (int c = 0; c < 256; c++)
    {
        uint32_t n = mac_roman_to_utf8((uint8_t)c, out);
        const uint8_t *p = out;

        TEST_ASSERT_EQ_INT(c, mac_roman_from_unicode(mac_roman_utf8_next(&p, out + n)));
    }
}

int main(void)
{
    TEST_RUN(test_table);
    TEST_RUN(test_utf8);
    return TEST_REPORT("mac_roman");
}
