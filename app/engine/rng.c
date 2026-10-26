/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* SplitMix64, the seed streams and Swift's random algorithms (rng.h). */
#include <stdarg.h>
#include <stddef.h>
#include "rng.h"

void bl_rng_seed(bl_rng_t *r, uint64_t seed) { r->state = seed; }

/* the 64-bit multiplies of the FM-1's CPU are bl_mul32's (f64.h; felucca-bsp docs/CPU.md): wrapping a * b */
__attribute__((noinline)) static uint64_t mul64(uint64_t a, uint64_t b)
{
    uint32_t al = (uint32_t)a, ah = (uint32_t)(a >> 32), bl = (uint32_t)b, bh = (uint32_t)(b >> 32);
    uint64_t lo = bl_mul32(al, bl);
    uint64_t cross = bl_mul32(al, bh) + bl_mul32(ah, bl);       /* only the low 32 bits of this reach the result */
    return lo + (cross << 32);
}

/* the full 64 x 64 -> 128 product, from four 32 x 32 -> 64 products */
__attribute__((noinline)) static void mul64x64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint32_t al = (uint32_t)a, ah = (uint32_t)(a >> 32), bl = (uint32_t)b, bh = (uint32_t)(b >> 32);
    uint64_t ll = bl_mul32(al, bl), lh = bl_mul32(al, bh), hl = bl_mul32(ah, bl), hh = bl_mul32(ah, bh);
    uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;    /* at most 3 * (2^32 - 1): no overflow */
    *lo = (mid << 32) | (uint32_t)ll;
    *hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}

/* ------------------------------------------------------------ SplitMix64 --- */
uint64_t bl_rng_next(bl_rng_t *r)
{
    uint64_t z = (r->state += UINT64_C(0x9E3779B97F4A7C15));
    z = mul64(z ^ (z >> 30), UINT64_C(0xBF58476D1CE4E5B9));
    z = mul64(z ^ (z >> 27), UINT64_C(0x94D049BB133111EB));
    return z ^ (z >> 31);
}

/* ----------------------------------------------------------- seed streams --- */
uint64_t bl_seed_derive(uint64_t master, ...)
{
    const uint64_t prime = UINT64_C(0x00000100000001B3);
    uint64_t hash = UINT64_C(0xCBF29CE484222325);
    va_list ap;
    const char *label;
    va_start(ap, master);
    while ((label = va_arg(ap, const char *)) != NULL) {
        for (; *label; label++) {
            hash ^= (uint8_t)*label;
            hash = mul64(hash, prime);
        }
        hash ^= 0x1F;                   /* the separator, so ("ab","c") and ("a","bc") differ */
        hash = mul64(hash, prime);
    }
    va_end(ap);
    bl_rng_t mixer;
    bl_rng_seed(&mixer, master ^ hash);
    return bl_rng_next(&mixer);
}

void bl_dec(int64_t v, char buf[24])
{
    char tmp[24];
    int n = 0, i = 0;
    uint64_t u = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
    do {
        tmp[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0) buf[i++] = '-';
    while (n) buf[i++] = tmp[--n];
    buf[i] = 0;
}

/* ------------------------------------------------- the standard library's --- */
/* FixedWidthInteger.random(in:using:) for a bound: Lemire's multiply, the rejection on the low half */
uint64_t bl_rand_below(bl_rng_t *r, uint64_t bound)
{
    uint64_t hi, lo;
    mul64x64(bl_rng_next(r), bound, &hi, &lo);
    if (lo < bound) {
        uint64_t t = (0 - bound) % bound;
        while (lo < t) mul64x64(bl_rng_next(r), bound, &hi, &lo);
    }
    return hi;
}

int64_t bl_rand_in(bl_rng_t *r, int64_t lo, int64_t hi)
{
    return (int64_t)((uint64_t)lo + bl_rand_below(r, (uint64_t)hi - (uint64_t)lo + 1));
}

int64_t bl_rand_upto(bl_rng_t *r, int64_t lo, int64_t hi)
{
    return (int64_t)((uint64_t)lo + bl_rand_below(r, (uint64_t)hi - (uint64_t)lo));
}

/* the bit Swift reads is bit 17 of the draw, 0 meaning true */
int bl_rand_bool(bl_rng_t *r) { return ((bl_rng_next(r) >> 17) & 1) == 0; }

/* the low 53 bits times 2^-53: both exact in binary64, so the product is exact */
f64 bl_rand_unit(bl_rng_t *r)
{
    return f64_mul(f64_from_u64(bl_rng_next(r) & ((UINT64_C(1) << 53) - 1)), F64(0x3CA0000000000000)); /* 2^-53 */
}

int64_t bl_rand_index(bl_rng_t *r, int64_t count) { return bl_rand_upto(r, 0, count); }
