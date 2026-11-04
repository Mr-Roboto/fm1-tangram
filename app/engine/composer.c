/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Bounded musical form, driven exclusively by live.c's arranger boundary. */
#include <string.h>
#include "composer.h"
#include "gen.h"

const char *const BL_COMP_NAME[7] = {"INTRO", "THEME A", "DEVELOP", "SECTION B", "RETURN", "BREAKDOWN", "OUTRO"};
static const uint8_t ORDER[7] = {
    BL_PH_BUILD, BL_PH_BED, BL_PH_VARIATION, BL_PH_BPART, BL_PH_SUBTRACT, BL_PH_PAYOFF, BL_PH_ENDING
};
static const uint8_t DWELL[7] = {1, 2, 2, 1, 1, 2, 1};

int bl_comp_phase(int section) { return ORDER[section >= 0 && section < 7 ? section : 0]; }

int bl_comp_valid(const bl_compcfg_t *c)
{
    return c->enabled <= 1 && (!c->bars || (c->bars >= 4 && c->bars <= 64 && !(c->bars % 4))) &&
        c->evolution <= 3 && c->tension <= 3;
}

static void phrase_unit(bl_composer_t *c, const bl_arrcfg_t *form)
{
    int lo = form->min_bars, hi = form->max_bars;
    if (lo < 4) lo = 4;
    if (lo > 64) lo = 64;
    if (hi < lo) hi = lo;
    if (hi > 64) hi = 64;
    int bars = (int)bl_rand_in(&c->rng, lo, hi);
    /* Ambient uses a longer plateau, still in four-bar phrases. */
    if (form->ambient && bars < 32) bars = 32;
    c->unit = (uint8_t)((bars + 3) / 4 * 4);
}

static void enter(bl_composer_t *c)
{
    c->phase = ORDER[c->section];
    c->bars_in_section = 0;
    c->bars_left = (c->cfg.bars ? c->cfg.bars : c->unit) * DWELL[c->section];
}

void bl_comp_select(bl_composer_t *c, int section)
{
    if (section < 0 || section >= 7 || section == c->section) return;
    c->section = (uint8_t)section;
    enter(c);
    c->enter_pending = 1;
}

void bl_comp_init(bl_composer_t *c, const bl_compcfg_t *cfg, const bl_arrcfg_t *form, uint64_t seed)
{
    memset(c, 0, sizeof *c);
    c->cfg = *cfg;
    bl_rng_seed(&c->rng, bl_seed_derive(seed, "composer", (const char *)0));
    phrase_unit(c, form);
    enter(c);
}

int bl_comp_tick(bl_composer_t *c, const bl_arrcfg_t *form, int advance)
{
    if (!c->cfg.enabled) return 0;
    c->bars_in_section++;
    if (--c->bars_left > 0 && !advance) return 0;
    if (++c->section == 7) {
        c->section = 0;
        c->cycle++;                    /* unsigned wrap is deterministic and bounded */
        phrase_unit(c, form);
    }
    enter(c);
    return 1;
}

int bl_comp_level(const bl_composer_t *c, int role)
{
    /* Roles: pluck 1, pluck 2, bass, pad, lead, drone, rhythm, auxiliary.
     * Source loops keep running even when a role's audible activity is zero. */
    static const uint8_t LEVEL[7][8] = {
        {70, 0, 45, 65, 0, 100, 0, 40},       /* introduction */
        {100, 80, 100, 70, 65, 80, 75, 60},    /* theme A */
        {100, 100, 100, 65, 85, 70, 90, 75},   /* development */
        {60, 100, 100, 80, 100, 60, 85, 65},   /* contrasting B */
        {100, 90, 100, 80, 80, 85, 85, 70},    /* return */
        {35, 0, 55, 70, 0, 100, 0, 30},       /* breakdown */
        {45, 0, 35, 60, 0, 100, 0, 25}        /* outro/transition */
    };
    if (role < 0 || role > BL_ROLE_AUX) return 100;
    int level = LEVEL[c->phase][role];
    /* Tension enriches B without touching rate, gate, pitch order or clocks. */
    if (c->phase == BL_PH_BPART && (role == BL_ROLE_LEAD || role == BL_ROLE_BED2))
        level = 70 + 10 * c->cfg.tension;
    /* Four bar activity ramps introduce/remove roles without stopping their
     * source clocks. These are density targets, never gate truncation or MIDI
     * volume messages. The initial drone remains the introduction's anchor. */
    if (c->bars_in_section < 4) {
        int previous = LEVEL[ORDER[c->section ? c->section - 1 : 6]][role];
        if (!c->cycle && !c->section) previous = role == BL_ROLE_DRONE ? 100 : 0;
        if (c->section && ORDER[c->section - 1] == BL_PH_BPART &&
            (role == BL_ROLE_LEAD || role == BL_ROLE_BED2)) previous = 70 + 10 * c->cfg.tension;
        level = previous + (level - previous) * (c->bars_in_section + 1) / 4;
    }
    return level;
}
