/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "arch/x86/cpu/cpuid.h"

static cpuid_raw_t sample(void)
{
    cpuid_raw_t raw = {0};

    /* "GenuineIntel" as EBX, EDX, ECX. */
    raw.max_leaf = 0x16;
    raw.vendor[0] = 0x756E6547;
    raw.vendor[1] = 0x49656E69;
    raw.vendor[2] = 0x6C65746E;
    /* Family 6, model 0x9E (extended model 9), stepping 10. */
    raw.l1[0] = 0x000906EA;
    raw.l1[2] = (1u << 0) | (1u << 28) | (1u << 31);
    raw.l1[3] = (1u << 0) | (1u << 4) | (1u << 16) | (1u << 25) | (1u << 26);
    raw.max_ext = 0x80000008u;
    raw.e1[3] = 1u << 20;
    raw.e7[3] = 1u << 8;

    return raw;
}

static void test_vendor_and_version(void)
{
    cpuid_raw_t raw = sample();
    hal_cpu_info_t info;

    cpuid_decode(&raw, &info);
    TEST_ASSERT_EQ_STR("GenuineIntel", info.vendor);
    TEST_ASSERT_EQ_INT(6, info.family);
    TEST_ASSERT_EQ_INT(0x9E, info.model);
    TEST_ASSERT_EQ_INT(10, info.stepping);
}

static void test_extended_family(void)
{
    cpuid_raw_t raw = sample();
    hal_cpu_info_t info;

    /* AMD Zen: base family 0xF + extended 0x8 = 0x17; model 0x31. */
    raw.l1[0] = 0x00830F10;
    cpuid_decode(&raw, &info);
    TEST_ASSERT_EQ_INT(0x17, info.family);
    TEST_ASSERT_EQ_INT(0x31, info.model);
}

static void test_features(void)
{
    cpuid_raw_t raw = sample();
    hal_cpu_info_t info;

    cpuid_decode(&raw, &info);
    TEST_ASSERT(info.features & HAL_CPU_FEATURE_FPU);
    TEST_ASSERT(info.features & HAL_CPU_FEATURE_TSC);
    TEST_ASSERT(info.features & HAL_CPU_FEATURE_SSE);
    TEST_ASSERT(info.features & HAL_CPU_FEATURE_SSE2);
    TEST_ASSERT(info.features & HAL_CPU_FEATURE_SSE3);
    TEST_ASSERT(info.features & HAL_CPU_FEATURE_AVX);
    TEST_ASSERT(info.features & HAL_CPU_FEATURE_HYPERVISOR);
    TEST_ASSERT(info.features & HAL_CPU_FEATURE_NX);
    TEST_ASSERT(info.features & HAL_CPU_FEATURE_PAT);
    TEST_ASSERT(info.features & HAL_CPU_FEATURE_INVARIANT_TSC);
    TEST_ASSERT(!(info.features & HAL_CPU_FEATURE_PAE));
    TEST_ASSERT(!(info.features & HAL_CPU_FEATURE_X2APIC));
}

static void test_brand_trimmed(void)
{
    cpuid_raw_t raw = sample();
    hal_cpu_info_t info;
    const char *name = "   Test CPU @ 1GHz";

    for (int i = 0; i < 48; i++)
    {
        char c = name[i];

        ((char *)raw.brand)[i] = c;
        if (c == '\0')
            break;
    }

    cpuid_decode(&raw, &info);
    TEST_ASSERT_EQ_STR("Test CPU @ 1GHz", info.brand);
}

static void test_missing_leaves(void)
{
    cpuid_raw_t raw = {0};
    hal_cpu_info_t info;

    raw.max_leaf = 0;
    cpuid_decode(&raw, &info);
    TEST_ASSERT_EQ_INT(0, info.features);
    TEST_ASSERT_EQ_STR("", info.brand);
}

int main(void)
{
    TEST_RUN(test_vendor_and_version);
    TEST_RUN(test_extended_family);
    TEST_RUN(test_features);
    TEST_RUN(test_brand_trimmed);
    TEST_RUN(test_missing_leaves);

    return TEST_REPORT("cpuid");
}
