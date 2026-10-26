/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* SoftFloat's build configuration for Berlinan (the file each of SoftFloat's build/<target>/ directories supplies;
 * SOURCE.md says why each choice). Every vendored file includes it first, f64.c too. */
#pragma once
#include <stdint.h>

#define LITTLEENDIAN 1          /* the FM-1's pi32v2 and the hosts */
#define SOFTFLOAT_FAST_INT64    /* uint64_t is native code on the pi32v2: shifts and adds in register pairs */
/* Not set, on purpose:
 *   INLINE_LEVEL                   every primitive stays an out-of-line function in its own s_*.c, so each 64-bit
 *                                  multiply sits in a helper of its own (felucca-bsp docs/CPU.md)
 *   SOFTFLOAT_FAST_DIV64TO32       would divide 64 by 32 bits: a libgcc call the freestanding firmware lacks
 *   SOFTFLOAT_FAST_DIV32TO16, SOFTFLOAT_ROUND_ODD, SOFTFLOAT_BUILTIN_CLZ, SOFTFLOAT_INTRINSIC_INT128
 *   THREAD_LOCAL                   (empty: one thread) */

/* SoftFloat's names for the operations f64.h defines on f64, renamed so the two sets link together */
#define f64_add sf_f64_add
#define f64_sub sf_f64_sub
#define f64_mul sf_f64_mul
#define f64_div sf_f64_div
#define f64_lt sf_f64_lt
#define f64_le sf_f64_le
#define f64_eq sf_f64_eq
#define f64_to_i64 sf_f64_to_i64

/* SoftFloat's 64 x 64 -> 128-bit product is f64.c's bl_mul128 (the same sums, on bl_mul32's products), not
 * s_mul64To128.c's, whose four multiplies the compiler put beside loads of its arguments, which the FM-1's CPU may
 * get wrong (felucca-bsp docs/CPU.md). The macro is SoftFloat's own hook: primitives.h declares the function only
 * while the name is not a macro */
struct uint128;
#define softfloat_mul64To128 bl_mul128
struct uint128 bl_mul128(uint64_t a, uint64_t b);

/* SoftFloat's code as it is: the host tests' -Wall -Wextra -Werror find a signed/unsigned comparison in
 * f64_to_i64_r_minMag.c that is correct there (f64.c restores the warnings for its own code) */
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-label"
