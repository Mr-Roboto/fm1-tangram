/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The arrangement state machine as Berlinan has it (Arrangement.swift): the autopilot that walks build, bed,
 * variation, B part, payoff, subtract and ending, one change per period of bars, and answers each change with
 * directives the engine obeys. A voice is its index in the project (Swift's voice id); a set of voices (locked,
 * adopted) is a mask of those indices. Not ported, as no template and no control reaches it: a config's own
 * buildOrder (the order is always derived from the roles). */
#pragma once
#include <stdint.h>
#include "bl.h"
#include "f64.h"
#include "rng.h"

/* ArrangementPhase, in its order */
enum { BL_PH_BUILD, BL_PH_BED, BL_PH_VARIATION, BL_PH_BPART, BL_PH_PAYOFF, BL_PH_SUBTRACT, BL_PH_ENDING };
extern const char *const BL_PHASE_RAW[7];  /* the raw values: "build" ... "ending" */

/* ArrangementConfig: enabled, ambientMode, reprise (nil is 0), minPeriodBars ... maxPeriodBars (1 <= min <= max) */
typedef struct {
    uint8_t enabled, ambient, reprise;
    int16_t min_bars, max_bars;
} bl_arrcfg_t;

/* the form presets: classicArc, slowBuild, ambientPlateau */
extern const bl_arrcfg_t bl_arr_classic, bl_arr_slow, bl_arr_ambient;

/* ArrangementDirective: voice for all but density (value, 0-1), bpart and stop */
enum {
    BL_DIR_ACTIVATE, BL_DIR_DEACTIVATE, BL_DIR_NUDGE, BL_DIR_ACCENT, BL_DIR_BLANK, BL_DIR_DENSITY, BL_DIR_BPART,
    BL_DIR_THIN, BL_DIR_STOP
};
typedef struct {
    uint8_t kind, voice;
    f64 value;
} bl_dir_t;

/* the most directives one change gives: the payoff's activations of every voice and its density */
#define BL_ARR_MAX_DIRS (BL_MAX_VOICES + 1)

/* ArrangementSM; active is activeVoices, an ordered list (build order as derived, then in the order voices joined) */
typedef struct {
    bl_arrcfg_t cfg;
    uint8_t phase;
    int32_t bars_in_phase;
    uint8_t nactive, active[BL_MAX_VOICES];
    uint8_t norder, order[BL_MAX_VOICES];   /* buildOrder */
    int32_t ambient_period;                 /* ambientPeriodBars, at least 8 */
    f64 density;
    bl_rng_t rng;
    int32_t bars_until_change;
    int32_t events_in_phase;
    int32_t round_robin;
    uint8_t stop_requested;
} bl_arr_t;

/* ArrangementSM.deriveBuildOrder(from:): the voices' indices, drone first, then bedPluck1, bedPluck2, pad, bass,
 * lead, then every other role in project order. Returns n */
int bl_build_order(const uint8_t *roles, int n, uint8_t *order);

/* ArrangementSM(config:voices:seed:adoptingActive:ambientPeriodBars:): roles[i] the role of voice i (BL_ROLE_*),
 * 1 <= nvoices <= BL_MAX_VOICES (Swift traps at the ending with no voice); adopting 0 is adoptingActive nil, else
 * active_mask the adopted set */
void bl_arr_init(bl_arr_t *a, const bl_arrcfg_t *cfg, const uint8_t *roles, int nvoices, uint64_t seed, int adopting,
                 uint32_t active_mask, int ambient_period_bars);
/* tickBar(locked:) and advance(locked:): the directives of the bar. Both return their number and write the first max
 * of them (as snprintf: a return above max means the rest are missing; BL_ARR_MAX_DIRS always holds them) */
int bl_arr_tick_bar(bl_arr_t *a, uint32_t locked_mask, bl_dir_t *out, int max);
int bl_arr_advance(bl_arr_t *a, uint32_t locked_mask, bl_dir_t *out, int max);
