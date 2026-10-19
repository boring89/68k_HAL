/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * File names: UTF-8 checks and the comparison form of a name (docs/ehfs.md
 * section 6, decisions E1 and E2): Unicode Normalization Form C (UAX #15:
 * canonical decomposition, canonical ordering, canonical composition), then
 * simple case folding (CaseFolding.txt status C and S), with the Unicode
 * 15.0 tables of unicode_data.h. Two names are the same name when their
 * comparison forms are the same bytes.
 */

#include "name.h"

#include "unicode_data.h"

#define MAX_CPS (EHFS_NAME_MAX * 4)     /* a decomposition is at most 4 times longer (UAX #15) */

/* Hangul syllables (Unicode 15.0, section 3.12) */
#define S_BASE 0xAC00u
#define L_BASE 0x1100u
#define V_BASE 0x1161u
#define T_BASE 0x11A7u
#define L_COUNT 19u
#define V_COUNT 21u
#define T_COUNT 28u
#define N_COUNT (V_COUNT * T_COUNT)
#define S_COUNT (L_COUNT * N_COUNT)

/* Decode one UTF-8 sequence; returns its length, 0 if it is not valid (overlong, surrogate, > U+10FFFF). */
static uint32_t decode(const uint8_t *s, uint32_t n, uint32_t *cp)
{
    uint32_t c = s[0], len, min;

    if (c < 0x80)
    {
        *cp = c;
        return 1;
    }
    if ((c & 0xE0) == 0xC0)
    {
        len = 2;
        min = 0x80;
        c &= 0x1F;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        len = 3;
        min = 0x800;
        c &= 0x0F;
    }
    else if ((c & 0xF8) == 0xF0)
    {
        len = 4;
        min = 0x10000;
        c &= 0x07;
    }
    else
    {
        return 0;
    }
    if (len > n)
        return 0;
    for (uint32_t i = 1; i < len; i++)
    {
        if ((s[i] & 0xC0) != 0x80)
            return 0;
        c = (c << 6) | (s[i] & 0x3F);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
        return 0;
    *cp = c;
    return len;
}

static uint32_t encode(uint32_t c, uint8_t *out)
{
    if (c < 0x80)
    {
        out[0] = (uint8_t)c;
        return 1;
    }
    if (c < 0x800)
    {
        out[0] = (uint8_t)(0xC0 | (c >> 6));
        out[1] = (uint8_t)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c < 0x10000)
    {
        out[0] = (uint8_t)(0xE0 | (c >> 12));
        out[1] = (uint8_t)(0x80 | ((c >> 6) & 0x3F));
        out[2] = (uint8_t)(0x80 | (c & 0x3F));
        return 3;
    }
    out[0] = (uint8_t)(0xF0 | (c >> 18));
    out[1] = (uint8_t)(0x80 | ((c >> 12) & 0x3F));
    out[2] = (uint8_t)(0x80 | ((c >> 6) & 0x3F));
    out[3] = (uint8_t)(0x80 | (c & 0x3F));
    return 4;
}

bool ehfs_name_valid(const uint8_t *name, uint32_t length)
{
    uint32_t cp;

    if (length == 0 || length > EHFS_NAME_MAX)
        return false;
    for (uint32_t i = 0; i < length;)
    {
        uint32_t n = decode(name + i, length - i, &cp);

        if (n == 0 || cp == 0 || cp == '/')
            return false;
        i += n;
    }
    return true;
}

static uint8_t combining_class(uint32_t cp)
{
    uint32_t lo = 0, hi = sizeof ehfs_ccc / sizeof ehfs_ccc[0];

    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2;

        if (ehfs_ccc[mid].cp == cp)
            return ehfs_ccc[mid].ccc;
        if (ehfs_ccc[mid].cp < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    return 0;
}

static int decomposition(uint32_t cp)
{
    uint32_t lo = 0, hi = sizeof ehfs_decomp / sizeof ehfs_decomp[0];

    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2;

        if (ehfs_decomp[mid].cp == cp)
            return (int)mid;
        if (ehfs_decomp[mid].cp < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}

/* Append the full canonical decomposition of cp; false if out is full. */
static bool decompose(uint32_t cp, uint32_t *out, uint32_t *n)
{
    if (cp >= S_BASE && cp < S_BASE + S_COUNT)
    {
        uint32_t s = cp - S_BASE;
        uint32_t t = s % T_COUNT;

        if (*n + 3 > MAX_CPS)
            return false;
        out[(*n)++] = L_BASE + s / N_COUNT;
        out[(*n)++] = V_BASE + (s % N_COUNT) / T_COUNT;
        if (t != 0)
            out[(*n)++] = T_BASE + t;
        return true;
    }

    int d = decomposition(cp);

    if (d < 0)
    {
        if (*n == MAX_CPS)
            return false;
        out[(*n)++] = cp;
        return true;
    }
    for (uint32_t i = 0; i < ehfs_decomp[d].len; i++)
    {
        if (!decompose(ehfs_decomp_cps[ehfs_decomp[d].at + i], out, n))
            return false;
    }
    return true;
}

/* The primary composite of a and b, or 0. */
static uint32_t composite(uint32_t a, uint32_t b)
{
    if (a >= L_BASE && a < L_BASE + L_COUNT && b >= V_BASE && b < V_BASE + V_COUNT)
        return S_BASE + ((a - L_BASE) * V_COUNT + (b - V_BASE)) * T_COUNT;
    if (a >= S_BASE && a < S_BASE + S_COUNT && (a - S_BASE) % T_COUNT == 0 && b > T_BASE &&
        b < T_BASE + T_COUNT)
        return a + (b - T_BASE);

    uint32_t lo = 0, hi = sizeof ehfs_compose / sizeof ehfs_compose[0];

    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2;

        if (ehfs_compose[mid].a == a && ehfs_compose[mid].b == b)
            return ehfs_compose[mid].c;
        if (ehfs_compose[mid].a < a || (ehfs_compose[mid].a == a && ehfs_compose[mid].b < b))
            lo = mid + 1;
        else
            hi = mid;
    }
    return 0;
}

static uint32_t fold(uint32_t cp)
{
    uint32_t lo = 0, hi = sizeof ehfs_fold / sizeof ehfs_fold[0];

    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2;

        if (ehfs_fold[mid].from == cp)
            return ehfs_fold[mid].to;
        if (ehfs_fold[mid].from < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    return cp;
}

/* NFC of the code points in cps (n of them), in place; returns the new count. */
static uint32_t nfc(uint32_t *cps, uint32_t n)
{
    /* Canonical ordering: sort each run of non-starters by class (stable). */
    for (uint32_t i = 1; i < n; i++)
    {
        uint8_t c = combining_class(cps[i]);

        for (uint32_t j = i; j > 0 && c != 0; j--)
        {
            uint8_t p = combining_class(cps[j - 1]);

            if (p <= c)
                break;

            uint32_t t = cps[j];

            cps[j] = cps[j - 1];
            cps[j - 1] = t;
        }
    }

    /*
     * Canonical composition (UAX #15's sample algorithm): a character joins the last starter unless a
     * character between them has class 0 or a class not below its own. A string that starts with a
     * non-starter has no starter until the next one (last class 256).
     */
    if (n == 0)
        return 0;

    uint32_t starter = 0, out = 1;
    uint32_t last = combining_class(cps[0]) == 0 ? 0 : 256;

    for (uint32_t i = 1; i < n; i++)
    {
        uint32_t c = cps[i];
        uint32_t cls = combining_class(c);
        uint32_t comp = composite(cps[starter], c);

        if (comp != 0 && (last < cls || last == 0))
        {
            cps[starter] = comp;
            continue;
        }
        if (cls == 0)
            starter = out;
        last = cls;
        cps[out++] = c;
    }
    return out;
}

int ehfs_name_key(const uint8_t *name, uint32_t length, uint8_t *out, uint32_t out_size)
{
    static uint32_t cps[MAX_CPS];
    uint32_t n = 0, cp;

    if (!ehfs_name_valid(name, length))
        return -1;
    for (uint32_t i = 0; i < length;)
    {
        i += decode(name + i, length - i, &cp);
        if (!decompose(cp, cps, &n))
            return -1;
    }
    n = nfc(cps, n);

    uint32_t len = 0;

    for (uint32_t i = 0; i < n; i++)
    {
        uint8_t buf[4];
        uint32_t k = encode(fold(cps[i]), buf);

        if (len + k > out_size)
            return -1;
        for (uint32_t j = 0; j < k; j++)
            out[len++] = buf[j];
    }
    return (int)len;
}
