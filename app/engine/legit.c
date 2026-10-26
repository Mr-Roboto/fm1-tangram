/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Legitimization and mutation (legit.h). Each draw is where Swift makes it: a guard's draw before the condition that
 * follows it (`Double.random(...) < p, !notes.isEmpty`), none from an empty collection's randomElement. The anchors
 * and the occupied positions are Swift Sets used only for membership: scans here. The products are 32-bit (a pattern
 * is shorter than 2^31 ticks): the FM-1 wants 64-bit multiplies out of line (felucca-bsp docs/CPU.md). */
#include "legit.h"
#include "gen.h"
#include "rng.h"

static const bl_meter_t M = {4, 4};                 /* Meter.fourFour */
static const int8_t STEPS[4] = {-2, -1, 1, 2};      /* the [-2, -1, 1, 2] the time nudge draws from */

/* AnchorPolicy.requiredLocalTicks(meter:) */
int bl_anchor_ticks(int anchor, bl_tick_t out[2])
{
    switch (anchor) {
    case BL_ANCHOR_BEAT1:
        out[0] = 0;
        return 1;
    case BL_ANCHOR_BEATS13:
        /* "beat 3" is two beats in; a bar of fewer than three beats has the downbeat only */
        out[0] = 0;
        if (M.beats < 3) return 1;
        out[1] = 2 * bl_ticks_per_beat(M);
        return 2;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------- lifecycle --- */
/* Legitimizer.advanceAtLoopBoundary: .repeating(r) with r > 1 counts down, any other .repeating (1, or 0) becomes
 * .established, which stays */
void bl_advance_boundary(bl_pattern_t *p)
{
    if (p->remaining > 1)
        p->remaining--;
    else
        p->remaining = -1;
}

/* Legitimizer.isMutable */
int bl_is_mutable(const bl_pattern_t *p) { return p->remaining == -1 && !p->locked; }

/* ------------------------------------------------------------- mutation --- */
/* anchors.contains(position): the set is bar * ticksPerBar + tick for each of the loop's whole bars (at least one:
 * max(bars, 1)) and each required tick */
static int anchored(const bl_pattern_t *p, const bl_tick_t *req, int nreq, bl_tick_t position)
{
    int32_t bars = (int32_t)(p->length / bl_ticks_per_bar(M));
    if (bars < 1) bars = 1;
    for (int32_t bar = 0; bar < bars; bar++)
        for (int i = 0; i < nreq; i++)
            if (position == bar * bl_ticks_per_bar(M) + req[i]) return 1;
    return 0;
}

/* the span guard's probe: max - min of the notes resolved at reference C 60, note idx replaced by the candidate */
static int32_t probe_span(const bl_key_t *key, const bl_pattern_t *p, int idx, bl_pitch_t cand)
{
    int32_t lo = 0, hi = 0;
    for (int i = 0; i < p->n; i++) {
        int32_t m = bl_resolve(key, i == idx ? cand : p->note[i].pitch, 60);
        if (i == 0 || m < lo) lo = m;
        if (i == 0 || m > hi) hi = m;
    }
    return hi - lo;
}

static int occupied(const bl_pattern_t *p, bl_tick_t position)
{
    for (int i = 0; i < p->n; i++)
        if (p->note[i].position == position) return 1;
    return 0;
}

/* Legitimizer.mutate(_:key:meter:seed:config:slidesEnabled:) with the slides off */
int bl_mutate(bl_voice_t *v, const bl_key_t *key, uint64_t seed, int repeats, f64 ratchet_p)
{
    const f64 P30 = F64(0x3FD3333333333333);        /* 0.30 */
    bl_pattern_t *p = &v->pat;
    bl_tick_t req[2];
    bl_rng_t r;
    int refused = 0;
    if (!bl_is_mutable(p)) return 0;
    bl_rng_seed(&r, seed);
    int32_t count = key->scale.count, step = bl_ticks_per_beat(M) / 4;
    int nreq = bl_anchor_ticks(v->anchor, req);

    /* 1. degree shifts: 30 % for each note not at the first (the least) position, by -count...count degrees, kept
     * only while the whole pattern spans two octaves at most; the bass's pitch never moves */
    if (v->role != BL_ROLE_BASS && p->n > 0) {
        bl_tick_t first = p->note[bl_first_sounding(p)].position;
        for (int i = 0; i < p->n; i++) {
            if (p->note[i].position == first) continue;
            if (!f64_lt(bl_rand_unit(&r), P30)) continue;
            int32_t delta = (int32_t)bl_rand_in(&r, -count, count);
            if (delta == 0) continue;
            bl_pitch_t cand = p->note[i].pitch;
            cand.degree += delta;
            if (probe_span(key, p, i, cand) <= 24)
                p->note[i].pitch = cand;
            else
                refused++;
        }
    }

    /* 2. time nudge: 30 % that one note off the anchors moves by 1-2 16ths, within the loop and onto a free tick */
    if (f64_lt(bl_rand_unit(&r), P30)) {
        int movable = 0;
        for (int i = 0; i < p->n; i++)
            if (!anchored(p, req, nreq, p->note[i].position)) movable++;
        if (movable > 0) {
            int64_t k = bl_rand_index(&r, movable);
            int idx = 0;
            for (; idx < p->n; idx++)
                if (!anchored(p, req, nreq, p->note[idx].position) && k-- == 0) break;
            bl_tick_t target = p->note[idx].position + STEPS[bl_rand_index(&r, 4)] * step;
            if (target >= 0 && target < p->length && !occupied(p, target)) p->note[idx].position = target;
        }
    }

    /* 3. accent move: 30 %, as PatternOps.moveAccents on this generator */
    if (f64_lt(bl_rand_unit(&r), P30) && p->n > 0) bl_move_accents_with(p, &r);

    /* 4. intermittent ratchet: every ratchet cleared, then ratchet_p that one note retriggers 2-3 times */
    for (int i = 0; i < p->n; i++) p->note[i].ratchet = 1;
    if (f64_lt(bl_rand_unit(&r), ratchet_p) && p->n > 0) {
        int64_t i = bl_rand_index(&r, p->n);
        p->note[i].ratchet = (uint8_t)bl_rand_in(&r, 2, 3);
    }

    /* legitimize the change: N verbatim plays before the next one */
    p->remaining = (int16_t)repeats;
    return refused;
}
