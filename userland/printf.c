/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Formatted output: integers, 64-bit numbers, floating point, strings. */

#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    printf("int: %d %u %x %o %5d|%-5d|%05d\n", -42, 42u, 255, 8, 42, 42, 42);
    printf("64-bit: %lld %llu %llx\n", -9000000000000LL, 18000000000000ULL, 0x123456789abcdefULL);
    printf("division: %lld %lld %llu\n", 9000000000000LL / 7, 9000000000000LL % 7,
           18446744073709551615ULL / 1000003ULL);
    printf("float: %.3f %.0f %e %g %g\n", 3.14159265, 2.5, 12345.6789, 0.0001, 1e20);
    printf("math: %.6f %.6f %.6f %.6f\n", sqrt(2.0), sin(1.0), exp(1.0), pow(2.0, 0.5));
    printf("string: [%s] [%10s] [%-10s] [%.3s]\n", "abc", "right", "left", "truncated");
    printf("char: %c%c%c %%\n", 'o', 'k', '!');
    printf("long double: %.4Lf\n", (long double)1 / 3);

    char buffer[64];
    int n = snprintf(buffer, sizeof(buffer), "%s-%d", "snprintf", 7);

    printf("%s (%d) len=%zu\n", buffer, n, strlen(buffer));

    return 0;
}
