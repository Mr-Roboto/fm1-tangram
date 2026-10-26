/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* IEEE binary64 without the C type for it: the FM-1's FPU is single precision and fm1-base's build rejects the
 * compiler's soft routines, so a Swift Double is here its bit pattern and the arithmetic is Berkeley SoftFloat's
 * (softfloat/, round to nearest even). An engine expression written as f64 calls in Swift's order (left to right,
 * one rounding per operation, no fused multiply-add) gives the Mac's bits. bl_sin is FreeBSD's sine on the same
 * arithmetic. */
#pragma once
#include <stdint.h>

typedef struct {
    uint64_t bits;
} f64;

/* a constant from its bit pattern; every Swift literal is written so, with its value in a comment:
 * F64(0x3FC3333333333333) for 0.15 */
#define F64(bits) ((f64){UINT64_C(bits)})

f64 f64_add(f64 a, f64 b);
f64 f64_sub(f64 a, f64 b);
f64 f64_mul(f64 a, f64 b);
f64 f64_div(f64 a, f64 b);
int f64_lt(f64 a, f64 b);           /* a < b, a <= b, a == b: false when either is a NaN */
int f64_le(f64 a, f64 b);
int f64_eq(f64 a, f64 b);
f64 f64_from_i64(int64_t v);        /* Double(v): to the nearest, ties to even */
f64 f64_from_u64(uint64_t v);
int64_t f64_to_i64(f64 x);          /* Int(x): toward zero; x finite and in range (Swift traps otherwise) */
f64 f64_round(f64 x);               /* x.rounded(): to the nearest integer, ties away from zero */
f64 f64_ceil(f64 x);                /* x.rounded(.up) */
f64 f64_frac1(f64 x);               /* x.truncatingRemainder(dividingBy: 1): x - trunc(x), the sign of x */
f64 f64_max(f64 x, f64 y);          /* Swift's max(x, y): y >= x ? y : x */
f64 f64_min(f64 x, f64 y);          /* Swift's min(x, y): y < x ? y : x */
f64 bl_sin(f64 x);                  /* sin(x) for |x| < 2^20 pi/2 (the lanes need [0, 2pi)); beyond: a trap */

/* a * b, the full 32 x 32 -> 64-bit product, in a function of its own that loads nothing: the FM-1's CPU may leave
 * a register a bit off when such a multiply runs beside a load (felucca-bsp docs/CPU.md), so the engine's and
 * SoftFloat's wider products (rng.c, f64.c's bl_mul128) are made of these */
uint64_t bl_mul32(uint32_t a, uint32_t b);
