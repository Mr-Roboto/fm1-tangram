/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "arrange.h"

/* Optional owner of the existing arranger boundary. Zero enabled preserves
 * the legacy arranger. bars=0 derives a phrase unit from the selected form. */
typedef struct {
    uint8_t enabled, bars, evolution, tension;
} bl_compcfg_t;

/* Display-independent targets. They bind only at the voice's existing local
 * completion; manual pending configurations remain a separate authority. */
typedef struct {
    uint32_t length;
    uint16_t rate;               /* 240/480 for ARP/MOTIF, zero for legacy */
    uint8_t reg_low, reg_high;
} bl_compmotion_t;

typedef struct {
    bl_compcfg_t cfg;
    bl_arrcfg_t legacy_form;      /* controller handover retains the previous AUTO intent */
    bl_rng_t rng;
    uint32_t cycle, manual_voices, requests;
    bl_tick_t started_at;
    bl_tick_t pending_tick;
    uint8_t pending_stage;       /* chronological index + 1, zero: no manual selection */
    uint8_t enter_pending;
    uint8_t start_pending;       /* fresh non-Intro entry, not a transition from Intro */
    uint8_t section, phase, unit, manual_harmony, manual_scene, legacy_enabled;
    uint8_t from_level[8];       /* actual previous activity, including interrupted ramps */
    uint8_t motion_voice, harmonic_step, release_from;
    int32_t harmonic_base;
    uint32_t motion_pending, motion_changed;
    bl_compmotion_t motion[BL_MAX_VOICES];
    int32_t bars_in_section, bars_left;
} bl_composer_t;

extern const char *const BL_COMP_NAME[7]; /* indexed by BL_PH_, not chronological section */
int bl_comp_valid(const bl_compcfg_t *cfg);
void bl_comp_init(bl_composer_t *c, const bl_compcfg_t *cfg, const bl_arrcfg_t *form, uint64_t seed);
/* After init only: selected section starts at its own activity/tension target.
 * Intro keeps its normal buildup; section age and phrase length stay intact. */
void bl_comp_start(bl_composer_t *c, int section);
/* One existing arranger bar/advance. Returns 1 only at a section transition. */
int bl_comp_tick(bl_composer_t *c, const bl_arrcfg_t *form, int advance);
/* Select an existing section without consuming RNG or changing cycle count. */
void bl_comp_select(bl_composer_t *c, int section);
int bl_comp_phase(int section);
int bl_comp_level(const bl_composer_t *c, int role);
