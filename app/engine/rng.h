/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Randomness as Berlinan has it: SplitMix64 (Rand.swift), the seed streams, and the algorithms of Swift 6.3.3's
 * standard library for Int.random(in:), Bool.random, Double.random(in:) and randomElement, so that every draw of the
 * engine gives the Mac's value. tests/equiv/rng.ref holds what Swift draws. */
#pragma once
#include <stdint.h>
#include "f64.h"

typedef struct {
    uint64_t state;
} bl_rng_t;

void bl_rng_seed(bl_rng_t *r, uint64_t seed);
uint64_t bl_rng_next(bl_rng_t *r);

/* SeedStream.derive(master:, labels...): the labels are C strings, ended by a NULL */
uint64_t bl_seed_derive(uint64_t master, ...);

/* String(v), the digits of a label such as "12" or "-240" */
void bl_dec(int64_t v, char buf[24]);

uint64_t bl_rand_below(bl_rng_t *r, uint64_t bound);              /* 0 <= x < bound, bound > 0 */
int64_t bl_rand_in(bl_rng_t *r, int64_t lo, int64_t hi);          /* Int.random(in: lo...hi), lo <= hi */
int64_t bl_rand_upto(bl_rng_t *r, int64_t lo, int64_t hi);        /* Int.random(in: lo..<hi), lo < hi */
int bl_rand_bool(bl_rng_t *r);                                    /* Bool.random */
f64 bl_rand_unit(bl_rng_t *r);                                    /* Double.random(in: 0..<1) */
int64_t bl_rand_index(bl_rng_t *r, int64_t count);                /* randomElement's index; count > 0 (an empty
                                                                   * collection draws nothing: the caller's case) */
