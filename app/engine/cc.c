/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The CC lanes and the scene morphs (cc.h). Swift compares each grid point's value with the point's before,
 * computing both; the value is a pure function of the tick, so here the one before is carried from the loop's last
 * step (computed afresh only at a window's first point): the same events with half the sines. */
#include "cc.h"

#define GRID 40                         /* ModulationEngine.gridTicks (ppqn / 24), sceneEmissions' ppqn * 4 / 96 */

#define ZERO F64(0x0000000000000000)
#define ONE F64(0x3FF0000000000000)
#define TWO F64(0x4000000000000000)
#define PI F64(0x400921FB54442D18)      /* Double.pi */
#define F127 F64(0x405FC00000000000)    /* 127.0 */

/* the first grid point at or after t, (t + 39) / 40 * 40 as Swift's Int (division toward zero); out of line, a
 * 64-bit multiply beside a load may corrupt a register on the FM-1 (felucca-bsp docs/CPU.md) */
__attribute__((noinline)) static bl_tick_t grid_up(bl_tick_t t) { return (t + GRID - 1) / GRID * GRID; }

/* one event into the caller's buffer, counted either way */
static void put(bl_cc_t *out, int max, int *n, bl_tick_t at, uint8_t voice, uint8_t controller, uint8_t value)
{
    if (*n < max) {
        out[*n].at = at;
        out[*n].voice = voice;
        out[*n].controller = controller;
        out[*n].value = value;
    }
    (*n)++;
}

/* -------------------------------------------------------------- lanes --- */
/* continuous's value(at:) for .sine: x = (Double(tick) / cycleTicks + phase).truncatingRemainder(dividingBy: 1), one
 * up when below 0; wave = sin(2 * .pi * x), (2 pi) x; v01 = max(0, min(1, center + depth / 2 * wave));
 * UInt8((v01 * 127).rounded()) */
static uint8_t lane_value(const bl_cclane_t *l, f64 cycle_ticks, bl_tick_t tick)
{
    f64 x = f64_frac1(f64_add(f64_div(f64_from_i64(tick), cycle_ticks), l->phase));
    if (f64_lt(x, ZERO)) x = f64_add(x, ONE);
    f64 wave = bl_sin(f64_mul(f64_mul(TWO, PI), x));
    f64 v01 = f64_max(ZERO, f64_min(ONE, f64_add(l->center, f64_mul(f64_div(l->depth, TWO), wave))));
    return (uint8_t)f64_to_i64(f64_round(f64_mul(v01, F127)));
}

/* ModulationEngine.continuous (and render's empty-range guard): cycleTicks = cycleBars * Double(ticksPerBar); from
 * the first grid point at or after lo, a point's value when it is tick 0 or differs from value(at: tick - 40) */
int bl_cc_lane(const bl_cclane_t *lane, uint8_t controller, uint8_t voice, bl_tick_t lo, bl_tick_t hi, bl_cc_t *out,
               int max)
{
    int n = 0;
    if (lo >= hi || lane->shape != BL_LFO_SINE) return 0;
    f64 cycle_ticks = f64_mul(lane->cycle_bars, f64_from_i64(BL_TICKS_PER_BAR));
    bl_tick_t t = grid_up(lo);
    uint8_t before = t != 0 && t < hi ? lane_value(lane, cycle_ticks, t - GRID) : 0;
    for (; t < hi; t += GRID) {
        uint8_t v = lane_value(lane, cycle_ticks, t);
        if (t == 0 || v != before) put(out, max, &n, t, voice, controller, v);
        before = v;
    }
    return n;
}

/* ------------------------------------------------------------- morphs --- */
/* LiveEngine.SceneMorph.value(at:): progress = Double(tick - startTick) / Double(endTick - startTick) */
f64 bl_morph_value(const bl_morph_t *m, bl_tick_t tick)
{
    if (tick <= m->t0) return m->start;
    if (tick >= m->t1) return m->end;
    f64 progress = f64_div(f64_from_i64(tick - m->t0), f64_from_i64(m->t1 - m->t0));
    return f64_add(m->start, f64_mul(f64_sub(m->end, m->start), progress));
}

/* sceneEmissions' UInt8(max(0, min(127, (morph.value(at: t) * 127).rounded()))) */
static uint8_t morph_level(const bl_morph_t *m, bl_tick_t t)
{
    return (uint8_t)f64_to_i64(f64_max(ZERO, f64_min(F127, f64_round(f64_mul(bl_morph_value(m, t), F127)))));
}

/* LiveEngine.sceneEmissions for one morph: firstGrid = the first grid point at or after startTick; t from
 * max(firstGrid, the first at or after lo); previous = nil at firstGrid (so it always sends), else the level at
 * t - 40; sent when previous != value */
int bl_morph_emit(const bl_morph_t *m, uint8_t voice, uint8_t controller, bl_tick_t lo, bl_tick_t hi, bl_cc_t *out,
                  int max)
{
    int n = 0;
    bl_tick_t first = grid_up(m->t0), t = grid_up(lo);
    if (first > t) t = first;
    uint8_t before = t != first && t < hi ? morph_level(m, t - GRID) : 0;
    for (; t < hi; t += GRID) {
        uint8_t v = morph_level(m, t);
        if (t == first || v != before) put(out, max, &n, t, voice, controller, v);
        before = v;
    }
    return n;
}
