/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The arrangement state machine (arrange.h). A change event writes its directives into a buffer of BL_ARR_MAX_DIRS
 * (the payoff's are the most: every voice and the density) and the caller's gets the first max of them; the state
 * moves the same whatever max is. */
#include <string.h>
#include "arrange.h"
#include "gen.h"

const char *const BL_PHASE_RAW[7] = {"build", "bed", "variation", "bPart", "payoff", "subtract", "ending"};

/* ArrangementConfig.classicArc (8-16 bars, reprise), .slowBuild (12-24, reprise), .ambientPlateau (ambient) */
const bl_arrcfg_t bl_arr_classic = {1, 0, 1, 8, 16};
const bl_arrcfg_t bl_arr_slow = {1, 0, 1, 12, 24};
const bl_arrcfg_t bl_arr_ambient = {1, 1, 0, 8, 16};

#define DENSITY_FULL F64(0x3FF0000000000000)    /* 1.0 */
#define DENSITY_STEP F64(0x3FEB333333333333)    /* 0.85 */
#define DENSITY_FLOOR F64(0x3FD3333333333333)   /* 0.3 */
#define DENSITY_SPARSE F64(0x3FD999999999999A)  /* 0.4 */

/* ArrangementSM.dwell: the change events of a phase before the next (bed, variation, bPart, payoff) */
static const int8_t DWELL[7] = {0, 3, 3, 1, 1, 0, 0};

/* ------------------------------------------------------------ build order --- */
/* deriveBuildOrder's rank: drone -1, bedPluck1 0, bedPluck2 1, pad 2, bass 3, lead 4, any other 5 + offset */
static int rank(const uint8_t *roles, int i)
{
    switch (roles[i]) {
    case BL_ROLE_DRONE: return -1;
    case BL_ROLE_BED1: return 0;
    case BL_ROLE_BED2: return 1;
    case BL_ROLE_PAD: return 2;
    case BL_ROLE_BASS: return 3;
    case BL_ROLE_LEAD: return 4;
    default: return 5 + i;
    }
}

/* ArrangementSM.deriveBuildOrder(from:): by rank, ties by offset; an insertion sort, stable, so the ties keep the
 * project order as Swift's offset comparison does */
int bl_build_order(const uint8_t *roles, int n, uint8_t *order)
{
    for (int i = 0; i < n; i++) {
        int j = i, r = rank(roles, i);
        for (; j > 0 && rank(roles, order[j - 1]) > r; j--) order[j] = order[j - 1];
        order[j] = (uint8_t)i;
    }
    return n;
}

/* ----------------------------------------------------------------- the SM --- */
/* activeVoices.contains */
static int is_active(const bl_arr_t *a, uint8_t v)
{
    for (int i = 0; i < a->nactive; i++)
        if (a->active[i] == v) return 1;
    return 0;
}

/* nextPeriod(): the fixed period while ambient, else Int.random(in: min...max) from the SM's own generator */
static int32_t next_period(bl_arr_t *a)
{
    if (a->cfg.ambient) return a->ambient_period;
    return (int32_t)bl_rand_in(&a->rng, a->cfg.min_bars, a->cfg.max_bars);
}

/* ArrangementSM.init: the build order from the roles; adopting, the adopted voices in build order (and the bed when
 * that is all of them), else the first voice alone in the build; then the first period */
void bl_arr_init(bl_arr_t *a, const bl_arrcfg_t *cfg, const uint8_t *roles, int nvoices, uint64_t seed, int adopting,
                 uint32_t active_mask, int ambient_period_bars)
{
    memset(a, 0, sizeof *a);
    a->cfg = *cfg;
    a->norder = (uint8_t)bl_build_order(roles, nvoices, a->order);
    a->ambient_period = ambient_period_bars > 8 ? ambient_period_bars : 8;     /* max(8, ambientPeriodBars) */
    a->density = DENSITY_FULL;
    bl_rng_seed(&a->rng, seed);
    if (adopting) {
        for (int i = 0; i < a->norder; i++)
            if (active_mask >> a->order[i] & 1) a->active[a->nactive++] = a->order[i];
        a->phase = a->nactive == a->norder ? BL_PH_BED : BL_PH_BUILD;
    } else {
        a->nactive = a->norder ? 1 : 0;                                        /* buildOrder.prefix(1) */
        a->active[0] = a->order[0];
        a->phase = BL_PH_BUILD;
    }
    a->bars_until_change = next_period(a);
}

/* nextTarget(locked:): round robin over the build order's active, unlocked voices; -1 when there is none (the
 * counter moves only when a voice is returned) */
static int next_target(bl_arr_t *a, uint32_t locked)
{
    uint8_t eligible[BL_MAX_VOICES];
    int n = 0;
    for (int i = 0; i < a->norder; i++)
        if (is_active(a, a->order[i]) && !(locked >> a->order[i] & 1)) eligible[n++] = a->order[i];
    if (!n) return -1;
    int target = eligible[a->round_robin % n];
    a->round_robin++;
    return target;
}

static int dir(bl_dir_t *d, uint8_t kind, int voice)
{
    *d = (bl_dir_t){kind, (uint8_t)voice, {0}};
    return 1;
}

static int density_dir(bl_dir_t *d, f64 value)
{
    *d = (bl_dir_t){BL_DIR_DENSITY, 0, value};
    return 1;
}

static int change_event(bl_arr_t *a, uint32_t locked, bl_dir_t *d);

/* enter(_:locked:): the phase from its start, and its first change event at once (the transition is the change) */
static int enter(bl_arr_t *a, uint8_t next, uint32_t locked, bl_dir_t *d)
{
    a->phase = next;
    a->bars_in_phase = 0;
    a->events_in_phase = 0;
    return change_event(a, locked, d);
}

/* changeEvent(locked:), phase by phase, into d (BL_ARR_MAX_DIRS) */
static int change_event(bl_arr_t *a, uint32_t locked, bl_dir_t *d)
{
    int t, n;
    switch (a->phase) {
    case BL_PH_BUILD:
        /* the first voice of the build order not yet active joins at the end */
        for (int i = 0; i < a->norder; i++)
            if (!is_active(a, a->order[i])) {
                a->active[a->nactive++] = a->order[i];
                return dir(d, BL_DIR_ACTIVATE, a->order[i]);
            }
        return enter(a, BL_PH_BED, locked, d);

    case BL_PH_BED:
        if (a->events_in_phase >= DWELL[BL_PH_BED]) return enter(a, BL_PH_VARIATION, locked, d);
        a->events_in_phase++;
        if ((t = next_target(a, locked)) < 0) return 0;
        return dir(d, BL_DIR_NUDGE, t);

    case BL_PH_VARIATION:
        if (a->events_in_phase >= DWELL[BL_PH_VARIATION]) return enter(a, BL_PH_BPART, locked, d);
        a->events_in_phase++;
        if ((t = next_target(a, locked)) < 0) return 0;
        /* a sparse mix rests more, a dense one changes more (only the subtract lowers the density, and the ending
         * follows it: no template reaches the rest here) */
        if (f64_lt(a->density, DENSITY_SPARSE)) return dir(d, BL_DIR_BLANK, t);
        switch (a->events_in_phase % 3) {
        case 1: return dir(d, BL_DIR_NUDGE, t);
        case 2: return dir(d, BL_DIR_ACCENT, t);
        default: return dir(d, BL_DIR_BLANK, t);
        }

    case BL_PH_BPART:
        if (a->events_in_phase >= DWELL[BL_PH_BPART]) return enter(a, BL_PH_PAYOFF, locked, d);
        a->events_in_phase++;
        return dir(d, BL_DIR_BPART, 0);

    case BL_PH_PAYOFF:
        if (a->events_in_phase >= DWELL[BL_PH_PAYOFF]) return enter(a, BL_PH_SUBTRACT, locked, d);
        a->events_in_phase++;
        /* everything back at full density: the missing voices in build order, which activeVoices becomes (the
         * build left none missing, so only the nudge is heard) */
        a->density = DENSITY_FULL;
        n = 0;
        for (int i = 0; i < a->norder; i++)
            if (!is_active(a, a->order[i])) n += dir(d + n, BL_DIR_ACTIVATE, a->order[i]);
        memcpy(a->active, a->order, a->norder);
        a->nactive = a->norder;
        if (!n && (t = next_target(a, locked)) >= 0) n = dir(d, BL_DIR_NUDGE, t);
        return n + density_dir(d + n, a->density);

    case BL_PH_SUBTRACT:
        /* the last to join leaves, the density down to 0.85 of itself, not below 0.3 */
        if (a->nactive > 1) {
            a->density = f64_max(DENSITY_FLOOR, f64_mul(a->density, DENSITY_STEP));
            dir(d, BL_DIR_DEACTIVATE, a->active[--a->nactive]);
            return 1 + density_dir(d + 1, a->density);
        }
        return enter(a, BL_PH_ENDING, locked, d);

    default:
        /* BL_PH_ENDING: the survivor thinned to roots (activeVoices.first ?? buildOrder[0]), then the stop */
        if (a->events_in_phase == 0) {
            a->events_in_phase = 1;
            return dir(d, BL_DIR_THIN, a->nactive ? a->active[0] : a->order[0]);
        }
        a->stop_requested = 1;
        return dir(d, BL_DIR_STOP, 0);
    }
}

/* the directives into the caller's buffer, as snprintf */
static int give(const bl_dir_t *d, int n, bl_dir_t *out, int max)
{
    for (int i = 0; i < n && i < max; i++) out[i] = d[i];
    return n;
}

/* tickBar(locked:): off or stopped, nothing; else one bar more in the phase and one less to the change, and when
 * the period is out a new one and the change event */
int bl_arr_tick_bar(bl_arr_t *a, uint32_t locked_mask, bl_dir_t *out, int max)
{
    bl_dir_t d[BL_ARR_MAX_DIRS];
    if (!a->cfg.enabled || a->stop_requested) return 0;
    a->bars_in_phase++;
    a->bars_until_change--;
    if (a->bars_until_change > 0) return 0;
    a->bars_until_change = next_period(a);
    return give(d, change_event(a, locked_mask, d), out, max);
}

/* advance(locked:): unless stopped (off or not), the period restarted and the change event now */
int bl_arr_advance(bl_arr_t *a, uint32_t locked_mask, bl_dir_t *out, int max)
{
    bl_dir_t d[BL_ARR_MAX_DIRS];
    if (a->stop_requested) return 0;
    a->bars_until_change = next_period(a);
    return give(d, change_event(a, locked_mask, d), out, max);
}
