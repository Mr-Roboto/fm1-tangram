/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* SoftFloat's build configuration for Berlinan (the file each of SoftFloat's build/<target>/ directories supplies;
 * SOURCE.md says why each choice). Every vendored file includes it first, f64.c too. */
#pragma once

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

/* SoftFloat's code as it is: the host tests' -Wall -Wextra -Werror find a signed/unsigned comparison in
 * f64_to_i64_r_minMag.c that is correct there (f64.c restores the warnings for its own code) */
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-label"
