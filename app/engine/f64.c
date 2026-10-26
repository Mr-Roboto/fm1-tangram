/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* IEEE binary64 on SoftFloat (f64.h), and FreeBSD's sine on it. */
#include "f64.h"
#pragma GCC diagnostic push             /* platform.h relaxes two warnings for SoftFloat's files, not for this one */
#include "softfloat/platform.h"
#include "softfloat/softfloat.h"
#include "softfloat/primitiveTypes.h"
#pragma GCC diagnostic pop
/* (platform.h renames SoftFloat's f64_add ... to sf_f64_add ...: the names below are this file's own) */
#undef f64_add
#undef f64_sub
#undef f64_mul
#undef f64_div
#undef f64_lt
#undef f64_le
#undef f64_eq
#undef f64_to_i64

/* an assert that holds in the firmware too: there a breakpoint, the crash screen */
#define BL_TRAP() __builtin_trap()

static float64_t sf(f64 x)
{
    float64_t r;
    r.v = x.bits;
    return r;
}
static f64 of(float64_t x)
{
    f64 r;
    r.bits = x.v;
    return r;
}

/* ------------------------------------------------------ the products --- */
__attribute__((noinline)) uint64_t bl_mul32(uint32_t a, uint32_t b) { return (uint64_t)a * b; }

/* softfloat_mul64To128 (platform.h): s_mul64To128.c's code with its products from bl_mul32 */
struct uint128 bl_mul128(uint64_t a, uint64_t b)
{
    uint32_t a32 = (uint32_t)(a >> 32), a0 = (uint32_t)a, b32 = (uint32_t)(b >> 32), b0 = (uint32_t)b;
    struct uint128 z;
    uint64_t mid1, mid;
    z.v0 = bl_mul32(a0, b0);
    mid1 = bl_mul32(a32, b0);
    mid = mid1 + bl_mul32(a0, b32);
    z.v64 = bl_mul32(a32, b32);
    z.v64 += (uint64_t)(mid < mid1) << 32 | mid >> 32;
    mid <<= 32;
    z.v0 += mid;
    z.v64 += (z.v0 < mid);
    return z;
}

/* ------------------------------------------------------------ arithmetic --- */
/* SoftFloat's rounding mode stays at its default, to nearest even: Swift's */
f64 f64_add(f64 a, f64 b) { return of(sf_f64_add(sf(a), sf(b))); }
f64 f64_sub(f64 a, f64 b) { return of(sf_f64_sub(sf(a), sf(b))); }
f64 f64_mul(f64 a, f64 b) { return of(sf_f64_mul(sf(a), sf(b))); }
f64 f64_div(f64 a, f64 b) { return of(sf_f64_div(sf(a), sf(b))); }
int f64_lt(f64 a, f64 b) { return sf_f64_lt(sf(a), sf(b)); }
int f64_le(f64 a, f64 b) { return sf_f64_le(sf(a), sf(b)); }
int f64_eq(f64 a, f64 b) { return sf_f64_eq(sf(a), sf(b)); }
f64 f64_from_i64(int64_t v) { return of(i64_to_f64(v)); }
f64 f64_from_u64(uint64_t v) { return of(ui64_to_f64(v)); }
int64_t f64_to_i64(f64 x) { return f64_to_i64_r_minMag(sf(x), false); }
f64 f64_round(f64 x) { return of(f64_roundToInt(sf(x), softfloat_round_near_maxMag, false)); }
f64 f64_ceil(f64 x) { return of(f64_roundToInt(sf(x), softfloat_round_max, false)); }

/* x - trunc(x) is exact; when it is zero, the remainder takes x's sign (fmod's, Swift's), where the subtraction
 * would give +0 for a negative integer */
f64 f64_frac1(f64 x)
{
    f64 r = f64_sub(x, of(f64_roundToInt(sf(x), softfloat_round_minMag, false)));
    if (r.bits << 1 == 0)
        r.bits = x.bits & 0x8000000000000000u;
    return r;
}

/* Swift's max and min (the Comparable ones, not IEEE's maximum): with a NaN or two zeros, which one comes back
 * follows the comparison */
f64 f64_max(f64 x, f64 y) { return f64_le(x, y) ? y : x; }
f64 f64_min(f64 x, f64 y) { return f64_lt(y, x) ? y : x; }

/* ------------------------------------------------------------------ sine --- */
/* FreeBSD msun's sin (lib/msun/src/s_sin.c, k_sin.c, k_cos.c, e_rem_pio2.c at tag release/14.1.0), transcribed onto
 * f64 operation for operation, in the C source's order (C's literals and constant products become F64 patterns and
 * products computed here, which round as the compiler's did). Only the arguments up to the medium reduction,
 * |x| < 2^20 pi/2, are transcribed (the CC lanes pass 2 pi x, x in [0, 1)); beyond it __kernel_rem_pio2 would be
 * needed, and bl_sin traps. Apple's libm, the Mac's sin, is not specified bit for bit: engine_test checks that the CC
 * values agree. msun's notice, for all four files (k_sin.c, k_cos.c and e_rem_pio2.c say SunSoft):
 *
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 *
 * e_rem_pio2.c: Optimized by Bruce D. Evans. */

static const f64 ONE = F64(0x3FF0000000000000),     /* 1.0 */
    HALF = F64(0x3FE0000000000000),                  /* 0.5 */
    /* k_sin.c */
    S1 = F64(0xBFC5555555555549),                    /* -1.66666666666666324348e-01 */
    S2 = F64(0x3F8111111110F8A6),                    /*  8.33333333332248946124e-03 */
    S3 = F64(0xBF2A01A019C161D5),                    /* -1.98412698298579493134e-04 */
    S4 = F64(0x3EC71DE357B1FE7D),                    /*  2.75573137070700676789e-06 */
    S5 = F64(0xBE5AE5E68A2B9CEB),                    /* -2.50507602534068634195e-08 */
    S6 = F64(0x3DE5D93A5ACFD57C),                    /*  1.58969099521155010221e-10 */
    /* k_cos.c */
    C1 = F64(0x3FA555555555554C),                    /*  4.16666666666666019037e-02 */
    C2 = F64(0xBF56C16C16C15177),                    /* -1.38888888888741095749e-03 */
    C3 = F64(0x3EFA01A019CB1590),                    /*  2.48015872894767294178e-05 */
    C4 = F64(0xBE927E4F809C52AD),                    /* -2.75573143513906633035e-07 */
    C5 = F64(0x3E21EE9EBDB4B1C4),                    /*  2.08757232129817482790e-09 */
    C6 = F64(0xBDA8FAE9BE8838D4),                    /* -1.13596475577881948265e-11 */
    /* e_rem_pio2.c */
    INVPIO2 = F64(0x3FE45F306DC9C883),               /* 53 bits of 2/pi: 6.36619772367581382433e-01 */
    PIO2_1 = F64(0x3FF921FB54400000),                /* first 33 bits of pi/2: 1.57079632673412561417e+00 */
    PIO2_1T = F64(0x3DD0B4611A626331),               /* pi/2 - pio2_1: 6.07710050650619224932e-11 */
    PIO2_2 = F64(0x3DD0B4611A600000),                /* second 33 bits: 6.07710050630396597660e-11 */
    PIO2_2T = F64(0x3BA3198A2E037073),               /* pi/2 - (pio2_1 + pio2_2): 2.02226624879595063154e-21 */
    PIO2_3 = F64(0x3BA3198A2E000000),                /* third 33 bits: 2.02226624871116645580e-21 */
    PIO2_3T = F64(0x397B839A252049C1),               /* pi/2 - (pio2_1 + pio2_2 + pio2_3): 8.47842766036889956997e-32 */
    RNINT = F64(0x4338000000000000);                 /* 0x1.8p52, math_private.h's rnint() */

static int32_t high_word(f64 x) { return (int32_t)(x.bits >> 32); }    /* GET_HIGH_WORD */
static f64 neg(f64 x)
{
    x.bits ^= 0x8000000000000000u;
    return x;
}

/* __kernel_sin(x, y, iy): sin(x + y) on ~[-pi/4, pi/4], y the tail of x (iy == 0: y is 0) */
static f64 kernel_sin(f64 x, f64 y, int iy)
{
    f64 z, r, v, w;
    z = f64_mul(x, x);
    w = f64_mul(z, z);
    /* r = S2+z*(S3+z*S4) + z*w*(S5+z*S6) */
    r = f64_add(f64_add(S2, f64_mul(z, f64_add(S3, f64_mul(z, S4)))),
                f64_mul(f64_mul(z, w), f64_add(S5, f64_mul(z, S6))));
    v = f64_mul(z, x);
    if (iy == 0)                                        /* x+v*(S1+z*r) */
        return f64_add(x, f64_mul(v, f64_add(S1, f64_mul(z, r))));
    /* x-((z*(half*y-v*r)-y)-v*S1) */
    return f64_sub(x, f64_sub(f64_sub(f64_mul(z, f64_sub(f64_mul(HALF, y), f64_mul(v, r))), y), f64_mul(v, S1)));
}

/* __kernel_cos(x, y): cos(x + y) on [-pi/4, pi/4] */
static f64 kernel_cos(f64 x, f64 y)
{
    f64 hz, z, r, w;
    z = f64_mul(x, x);
    w = f64_mul(z, z);
    /* r = z*(C1+z*(C2+z*C3)) + w*w*(C4+z*(C5+z*C6)) */
    r = f64_add(f64_mul(z, f64_add(C1, f64_mul(z, f64_add(C2, f64_mul(z, C3))))),
                f64_mul(f64_mul(w, w), f64_add(C4, f64_mul(z, f64_add(C5, f64_mul(z, C6))))));
    hz = f64_mul(HALF, z);                              /* 0.5*z */
    w = f64_sub(ONE, hz);
    /* w + (((one-w)-hz) + (z*r-x*y)) */
    return f64_add(w, f64_add(f64_sub(f64_sub(ONE, w), hz), f64_sub(f64_mul(z, r), f64_mul(x, y))));
}

/* e_rem_pio2.c's small cases, |x| ~<= 9pi/4 away from the multiples of pi/2: x - k pi/2 in two pieces, k = 1..4
 * (x < 0: -k). For x > 0: z = x - k*pio2_1; y[0] = z - k*pio2_1t; y[1] = (z-y[0])-k*pio2_1t; for x < 0 the same
 * with + (k*pio2_1 and k*pio2_1t: the compiler's constant products; 1*c is c) */
static int32_t rem_small(f64 x, f64 *y, int32_t k, int pos)
{
    f64 kc = f64_mul(f64_from_i64(k), PIO2_1), kct = f64_mul(f64_from_i64(k), PIO2_1T), z;
    if (pos) {
        z = f64_sub(x, kc);
        y[0] = f64_sub(z, kct);
        y[1] = f64_sub(f64_sub(z, y[0]), kct);
        return k;
    }
    z = f64_add(x, kc);
    y[0] = f64_add(z, kct);
    y[1] = f64_add(f64_sub(z, y[0]), kct);
    return -k;
}

/* __ieee754_rem_pio2(x, y): x - n pi/2 = y[0] + y[1], |x| > pi/4; returns n */
static int32_t rem_pio2(f64 x, f64 *y)
{
    f64 w, t, r, fn;
    int32_t i, j, n, ix, hx;
    uint32_t high;

    hx = high_word(x);
    ix = hx & 0x7fffffff;
    if (ix <= 0x400f6a7a) {                             /* |x| ~<= 5pi/4 */
        if ((ix & 0xfffff) == 0x921fb)                  /* |x| ~= pi/2 or 2pi/2: cancellation, the medium case */
            goto medium;
        return rem_small(x, y, ix <= 0x4002d97c ? 1 : 2, hx > 0);   /* |x| ~<= 3pi/4: 1, else 2 */
    }
    if (ix <= 0x401c463b) {                             /* |x| ~<= 9pi/4 */
        if (ix <= 0x4015fdbc) {                         /* |x| ~<= 7pi/4 */
            if (ix == 0x4012d97c)                       /* |x| ~= 3pi/2 */
                goto medium;
            return rem_small(x, y, 3, hx > 0);
        }
        if (ix == 0x401921fb)                           /* |x| ~= 4pi/2 */
            goto medium;
        return rem_small(x, y, 4, hx > 0);
    }
    if (ix >= 0x413921fb)                               /* large (and inf, NaN): not transcribed */
        BL_TRAP();
medium:
    fn = f64_sub(f64_add(f64_mul(x, INVPIO2), RNINT), RNINT);      /* rnint((double_t)x*invpio2) */
    n = (int32_t)f64_to_i64(fn);                                    /* irint(fn) */
    r = f64_sub(x, f64_mul(fn, PIO2_1));                            /* x-fn*pio2_1 */
    w = f64_mul(fn, PIO2_1T);                                       /* 1st round good to 85 bits */
    j = ix >> 20;
    y[0] = f64_sub(r, w);
    high = (uint32_t)high_word(y[0]);
    i = j - (int32_t)((high >> 20) & 0x7ff);
    if (i > 16) {                                       /* 2nd iteration needed, good to 118 */
        t = r;
        w = f64_mul(fn, PIO2_2);
        r = f64_sub(t, w);
        w = f64_sub(f64_mul(fn, PIO2_2T), f64_sub(f64_sub(t, r), w));   /* fn*pio2_2t-((t-r)-w) */
        y[0] = f64_sub(r, w);
        high = (uint32_t)high_word(y[0]);
        i = j - (int32_t)((high >> 20) & 0x7ff);
        if (i > 49) {                                   /* 3rd iteration, 151 bits */
            t = r;
            w = f64_mul(fn, PIO2_3);
            r = f64_sub(t, w);
            w = f64_sub(f64_mul(fn, PIO2_3T), f64_sub(f64_sub(t, r), w));
            y[0] = f64_sub(r, w);
        }
    }
    y[1] = f64_sub(f64_sub(r, y[0]), w);                /* (r-y[0])-w */
    return n;
}

f64 bl_sin(f64 x)
{
    f64 y[2], z = F64(0);
    int32_t n, ix;

    ix = high_word(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) {                             /* |x| ~< pi/4 */
        if (ix < 0x3e500000)                            /* |x| < 2^-26: x (msun's (int)x == 0 only raises inexact) */
            return x;
        return kernel_sin(x, z, 0);
    }
    if (ix >= 0x7ff00000)                               /* sin(inf or NaN) is NaN: x-x */
        return f64_sub(x, x);
    n = rem_pio2(x, y);
    switch (n & 3) {
    case 0:
        return kernel_sin(y[0], y[1], 1);
    case 1:
        return kernel_cos(y[0], y[1]);
    case 2:
        return neg(kernel_sin(y[0], y[1], 1));
    default:
        return neg(kernel_cos(y[0], y[1]));
    }
}
