/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * The 64-bit division helpers that GCC calls on 32-bit x86 (libgcc's
 * __udivdi3, __umoddi3, __divdi3, __moddi3). The SDK cannot use the
 * host's libgcc because it is 64-bit and the host has no 32-bit one.
 * When both operands fit in 32 bits the hardware DIV does the work,
 * otherwise a shift-and-subtract loop.
 */

typedef unsigned long long u64;
typedef long long s64;
typedef unsigned int u32;

static u64 udivmod(u64 n, u64 d, u64 *rem)
{
    if ((n >> 32) == 0 && (d >> 32) == 0)
    {
        u32 n32 = (u32)n;
        u32 d32 = (u32)d;

        if (rem)
            *rem = n32 % d32;

        return n32 / d32;
    }

    u64 q = 0;
    u64 r = 0;

    for (int i = 63; i >= 0; i--)
    {
        r = (r << 1) | ((n >> i) & 1);

        if (r >= d)
        {
            r -= d;
            q |= (u64)1 << i;
        }
    }

    if (rem)
        *rem = r;

    return q;
}

u64 __udivmoddi4(u64 n, u64 d, u64 *rem)
{
    return udivmod(n, d, rem);
}

#ifndef RT_ONLY_DIVMOD4 /* tcc's own libtcc1 has these four */
u64 __udivdi3(u64 n, u64 d)
{
    return udivmod(n, d, 0);
}

u64 __umoddi3(u64 n, u64 d)
{
    u64 r;

    udivmod(n, d, &r);

    return r;
}

s64 __divdi3(s64 n, s64 d)
{
    int negative = (n < 0) != (d < 0);
    u64 q = udivmod(n < 0 ? -(u64)n : (u64)n, d < 0 ? -(u64)d : (u64)d, 0);

    return negative ? -(s64)q : (s64)q;
}

s64 __moddi3(s64 n, s64 d)
{
    u64 r;

    udivmod(n < 0 ? -(u64)n : (u64)n, d < 0 ? -(u64)d : (u64)d, &r);

    return n < 0 ? -(s64)r : (s64)r;
}

#endif /* RT_ONLY_DIVMOD4 */

s64 __divmoddi4(s64 n, s64 d, s64 *rem)
{
    int n_negative = n < 0;
    int d_negative = d < 0;
    u64 r;
    u64 q = udivmod(n_negative ? -(u64)n : (u64)n, d_negative ? -(u64)d : (u64)d, &r);

    if (rem)
        *rem = n_negative ? -(s64)r : (s64)r;

    return n_negative != d_negative ? -(s64)q : (s64)q;
}
