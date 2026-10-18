/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/config/config.h"

static void test_parse(void)
{
    static const char text[] =
        "# 68k_HAL settings\n"
        "autostart = mac\n"
        "  mac_scale=1.5   # HiDPI\r\n"
        "mac_res =2560x1440\n"
        "no equal sign here\n"
        "\n"
        "mac_ram = 128\n"
        "mac_ram = 256\n";

    config_reset();
    TEST_ASSERT_EQ_INT(5, config_parse(text, sizeof text - 1));
    TEST_ASSERT_EQ_STR("mac", config_get("autostart"));
    TEST_ASSERT_EQ_STR("1.5", config_get("mac_scale"));
    TEST_ASSERT_EQ_STR("2560x1440", config_get("mac_res"));
    TEST_ASSERT_EQ_STR("256", config_get("mac_ram"));
    TEST_ASSERT(config_get("no equal sign here") == NULL);
    TEST_ASSERT(config_get("missing") == NULL);
    TEST_ASSERT_EQ_STR("x", config_get_or("missing", "x"));
}

static void test_no_final_newline_and_nul(void)
{
    config_reset();
    TEST_ASSERT_EQ_INT(1, config_parse("a=1", 3));
    TEST_ASSERT_EQ_STR("1", config_get("a"));

    /* A NUL ends the text (a file padded with zeros). */
    config_reset();
    TEST_ASSERT_EQ_INT(1, config_parse("b=2\n\0c=3\n", 10));
    TEST_ASSERT(config_get("c") == NULL);
}

static void test_limits(void)
{
    char line[256];

    config_reset();
    memset(line, 'k', sizeof line);
    memcpy(line + 200, "=v\n", 3);
    TEST_ASSERT_EQ_INT(0, config_parse(line, 203));       /* key too long */
    TEST_ASSERT_EQ_INT(0, config_parse("=v\n", 3));       /* empty key */
    TEST_ASSERT_EQ_INT(1, config_parse("k=\n", 3));       /* empty value is fine */
    TEST_ASSERT_EQ_STR("", config_get("k"));

    config_reset();
    for (int i = 0; i < CONFIG_MAX_ENTRIES; i++)
    {
        char key[8];

        snprintf(key, sizeof key, "k%d", i);
        TEST_ASSERT_EQ_INT(0, config_set(key, "v"));
    }
    TEST_ASSERT_EQ_INT(-1, config_set("one_more", "v"));
    TEST_ASSERT_EQ_INT(0, config_set("k3", "replaced"));   /* replacing still works */
    TEST_ASSERT_EQ_STR("replaced", config_get("k3"));
}

static void test_cmdline(void)
{
    const char *key;
    const char *value;

    config_reset();
    TEST_ASSERT_EQ_INT(2, config_parse("autostart=shell\nmac_res=640x480\n", 32));
    TEST_ASSERT_EQ_INT(2, config_apply_cmdline("/68k_hal/slot_a/kernel.elf autostart=mac diag slot=b"));
    TEST_ASSERT_EQ_STR("mac", config_get("autostart"));
    TEST_ASSERT_EQ_STR("b", config_get("slot"));
    TEST_ASSERT_EQ_STR("640x480", config_get("mac_res"));
    TEST_ASSERT(config_get("diag") == NULL);
    TEST_ASSERT_EQ_INT(0, config_apply_cmdline(NULL));
    TEST_ASSERT_EQ_INT(0, config_apply_cmdline("   "));

    TEST_ASSERT_EQ_INT(0, config_entry(0, &key, &value));
    TEST_ASSERT_EQ_STR("autostart", key);
    TEST_ASSERT_EQ_INT(-1, config_entry(3, &key, &value));
}

int main(void)
{
    TEST_RUN(test_parse);
    TEST_RUN(test_no_final_newline_and_nul);
    TEST_RUN(test_limits);
    TEST_RUN(test_cmdline);

    return TEST_REPORT("config");
}
