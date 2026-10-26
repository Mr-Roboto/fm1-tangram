/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The generators the templates use, as Berlinan has them (Generators.swift): every one a pure function of config,
 * role, key and seed, in 4/4, drawing from one SplitMix64 in Swift's order. Not ported, as no template and no
 * control reaches them: highMidLow, drunkardsWalk, arpeggiator and the decoupled gate cycles. */
#pragma once
#include <stdint.h>
#include "music.h"
#include "pattern.h"

/* VoiceRole's order */
enum {
    BL_ROLE_BED1, BL_ROLE_BED2, BL_ROLE_BASS, BL_ROLE_PAD, BL_ROLE_LEAD, BL_ROLE_DRONE, BL_ROLE_RHYTHM, BL_ROLE_AUX
};

/* GeneratorConfig's cases that are ported */
enum {
    BL_GEN_SEED_PROCEDURE, BL_GEN_PULSING_BASS, BL_GEN_STAGED_PLUCK, BL_GEN_BASS_MELODY, BL_GEN_CHORD_LAYER,
    BL_GEN_HEARTBEAT, BL_GEN_DRONE
};

/* seedProcedure: a = bars, b = mutations; pulsingBass: a = accents; stagedPluck, bassMelody, drone: a = bars;
 * chordLayer: a = periodBars; heartbeat: none */
typedef struct {
    uint8_t kind;
    int16_t a, b;
} bl_gencfg_t;

struct bl_sched;                /* harmony.h */

/* PatternFactory.make(id:config:role:key:meter:seed:); the bass melody here walks over the pedal */
void bl_make_pattern(const bl_gencfg_t *cfg, int role, const bl_key_t *key, uint64_t seed, bl_pattern_t *out);
/* PatternFactory.makeBassMelody(id:bars:schedule:meter:seed:); a NULL schedule is the pedal */
void bl_make_bass_melody(int bars, const struct bl_sched *sched, uint64_t seed, bl_pattern_t *out);
