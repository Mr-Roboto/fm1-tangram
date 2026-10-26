/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Legitimization as Berlinan has it (Legitimizer.swift, Voice.swift): the engine's voice and its anchor rule, the
 * lifecycle a pattern counts down at its loop boundaries ("repetition legitimizes"), and the one constrained
 * mutation an established, unlocked pattern may take, in 4/4. Not ported, as no template reaches them: the
 * accumulator drift, the slides (step 5: no template opts in), the voice's density cap. */
#pragma once
#include <stdint.h>
#include "bl.h"
#include "f64.h"
#include "music.h"
#include "pattern.h"

/* AnchorPolicy (in this order here, not Swift's) */
enum { BL_ANCHOR_NONE, BL_ANCHOR_BEAT1, BL_ANCHOR_BEATS13 };

/* Voice: role a BL_ROLE_ of gen.h, the register window reg_low...reg_high, ref_c its referenceC, participates its
 * participatesInTransposition, anchor a BL_ANCHOR_ */
typedef struct {
    const char *id;
    uint8_t role, reg_low, reg_high, participates, anchor;
    int32_t ref_c;
    bl_pattern_t pat;
} bl_voice_t;

/* Legitimizer.Config's ratchetProbability nil: 0.15 */
#define BL_RATCHET_DEFAULT F64(0x3FC3333333333333)

/* AnchorPolicy.requiredLocalTicks(meter:): the ticks within a bar that must carry an onset; returns their count */
int bl_anchor_ticks(int anchor, bl_tick_t out[2]);

/* Legitimizer.advanceAtLoopBoundary, in place: a repeating pattern counts down, the last play makes it established */
void bl_advance_boundary(bl_pattern_t *p);
/* Legitimizer.isMutable: established and not locked */
int bl_is_mutable(const bl_pattern_t *p);

/* Legitimizer.mutate(_:key:meter:seed:config:slidesEnabled: false), in place: the identity unless the pattern is
 * mutable; else the degree shifts, the time nudge, the accent move and the ratchet, and the lifecycle restarts at
 * repeats (2..32). ratchet_p is the config's ratchetProbability (BL_RATCHET_DEFAULT for nil). Returns the number of
 * degree shifts the span guard refused */
int bl_mutate(bl_voice_t *v, const bl_key_t *key, uint64_t seed, int repeats, f64 ratchet_p);
