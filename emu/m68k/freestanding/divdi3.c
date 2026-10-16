/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * 64-bit division helpers. On 32-bit x86 GCC turns 64-bit '/' and '%' into
 * calls to these libgcc functions; the kernel is linked without libgcc, and
 * Musashi needs them for DIVU.L/DIVS.L with a 64-bit dividend. Plain
 * shift-and-subtract long division; written without '/' and '%' so it does
 * not call itself.
 */

typedef unsigned long long u64;
typedef long long s64;


u64 __udivmoddi4(u64 n, u64 d, u64 *rem)
{
    u64 q = 0;
    u64 r = 0;

    /* Division by zero is the caller's problem (Musashi checks first). */
    for (int i = 63; i >= 0; i--)
    {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d)
        {
            r -= d;
            q |= (u64)1 << i;
        }
    }
    if (rem != 0)
    {
        *rem = r;
    }
    return q;
}


u64 __udivdi3(u64 n, u64 d)
{
    return __udivmoddi4(n, d, 0);
}


/* Truncating division: the quotient takes the sign of n^d, the remainder
 * the sign of n (C99 6.5.5). */
s64 __divmoddi4(s64 n, s64 d, s64 *rem)
{
    int n_negative = n < 0;
    int d_negative = d < 0;
    u64 un = n_negative ? -(u64)n : (u64)n;
    u64 ud = d_negative ? -(u64)d : (u64)d;
    u64 ur;
    u64 uq = __udivmoddi4(un, ud, &ur);

    if (rem != 0)
    {
        *rem = n_negative ? -(s64)ur : (s64)ur;
    }
    return (n_negative != d_negative) ? -(s64)uq : (s64)uq;
}


u64 __umoddi3(u64 n, u64 d)
{
    u64 r;

    __udivmoddi4(n, d, &r);
    return r;
}


s64 __divdi3(s64 n, s64 d)
{
    return __divmoddi4(n, d, 0);
}


s64 __moddi3(s64 n, s64 d)
{
    s64 r;

    __divmoddi4(n, d, &r);
    return r;
}
