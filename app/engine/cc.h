/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The CC lanes and the scene morphs as Berlinan has them: a lane is ModulationEngine's continuous LFO
 * (Modulation.swift), a morph LiveEngine's SceneMorph, a straight glide from one value to another over bars. Both are
 * read on a grid of 40 ticks (a 1/96 note) and send a value only when it changes, from the descriptor alone: no state
 * moves per window, so chained windows give the whole range's stream. Here one lane or one morph per call; the rest
 * is the live engine's (live.c):
 * - ModulationEngine.render: each of the voice's lanes in order, its target looked up in the voice's CC map (an
 *   unmapped lane is skipped), then all of the voice's events sorted by (at, controller), a stable sort;
 * - sceneEmissions: the morphs by key ("voiceID|target", string order), each with its voice's controller for the
 *   target (no voice or no controller: skipped), concatenated, not sorted;
 * - startSceneMorph: start = the key's morph's value at the bind tick (0.5 when none), end = max(0, min(1,
 *   destination)), t1 = t0 + max(1, bars) bars; a renderCC window sees the morphs as they stand after its render,
 *   which binds every change before the window's end.
 * Not ported, as no template has them: the lane shapes other than sine (triangle, sawUp, sawDown, square,
 * sampleHold); `shape` is there for the saved format, and a lane of another shape renders nothing. The meter is 4/4
 * (BL_TICKS_PER_BAR). */
#pragma once
#include <stdint.h>
#include "bl.h"
#include "f64.h"

/* LFOShape: sine only in v1 */
enum { BL_LFO_SINE };

/* CCLane: cycleBars 0.5-64, depth and center 0-1, phase 0..<1 (CCLane's preconditions); target the index of its
 * name in the voice's CC map. preStrikeTicks is sampleHold's alone */
typedef struct {
    uint8_t shape;                  /* sine only in v1 */
    uint8_t target;                 /* index into the voice's cc map */
    f64 cycle_bars, depth, center, phase;
} bl_cclane_t;

/* RenderedCC; voice is the voice's index in the project (Swift's voiceID) */
typedef struct {
    bl_tick_t at;
    uint8_t voice, controller, value;
} bl_cc_t;

/* ModulationEngine.continuous for a sine lane over lo..<hi: on every grid point t (multiples of 40), the value
 * UInt8((max(0, min(1, center + depth / 2 * sin(2 pi x))) * 127).rounded()), x = (t / (cycleBars * 3840) + phase)
 * mod 1, sent when it differs from the grid point's before (t - 40) and always at tick 0. Returns the number of
 * events and writes the first max of them (as snprintf: a return above max means the rest are missing) */
int bl_cc_lane(const bl_cclane_t *lane, uint8_t controller, uint8_t voice, bl_tick_t lo, bl_tick_t hi, bl_cc_t *out,
               int max);

/* SceneMorph: start and end 0-1, startTick t0 < endTick t1 */
typedef struct {
    f64 start, end;
    bl_tick_t t0, t1;
} bl_morph_t;

/* SceneMorph.value(at:): start up to t0, end from t1, start + (end - start) * (tick - t0) / (t1 - t0) between */
f64 bl_morph_value(const bl_morph_t *m, bl_tick_t tick);
/* sceneEmissions for one morph over lo..<hi: on every grid point t from the first at or after t0, the value
 * UInt8(max(0, min(127, (value(at: t) * 127).rounded()))), sent at that first point and whenever it differs from
 * the point's before. Returns and writes as bl_cc_lane */
int bl_morph_emit(const bl_morph_t *m, uint8_t voice, uint8_t controller, bl_tick_t lo, bl_tick_t hi, bl_cc_t *out,
                  int max);
