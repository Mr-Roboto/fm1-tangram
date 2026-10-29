/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The generators the templates use, as Berlinan has them (Generators.swift): every one a pure function of config,
 * role, key and seed, in 4/4, drawing from one SplitMix64 in Swift's order. Not ported, as no template and no
 * control reaches them: highMidLow, drunkardsWalk and the decoupled gate cycles.
 * Tangram's ARP and stable seeded MOTIF extend this factory without changing the templates. */
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
    BL_GEN_HEARTBEAT, BL_GEN_DRONE, BL_GEN_ARP, BL_GEN_MOTIF, BL_NGENS
};

/* seedProcedure: a = bars, b = mutations; pulsingBass: a = accents; stagedPluck, bassMelody, drone: a = bars;
 * chordLayer: a = periodBars; heartbeat: none.
 * ARP/MOTIF: a = onset spacing in ticks (480 eighths or 240 sixteenths; 0/invalid -> 480);
 * b = gate ticks (0 -> half spacing; negative or >= spacing -> half spacing).
 * Gate is independent of rate. UP, two octaves, one bar before phase_steps. */
typedef struct {
    uint8_t kind;
    int16_t a, b;
} bl_gencfg_t;

struct bl_sched;                /* harmony.h */

/* PatternFactory.make(id:config:role:key:meter:seed:); the bass melody here walks over the pedal. Both return the
 * number of notes that did not fit in BL_MAX_NOTES and were left out: 0 for every config the templates use */
int bl_make_pattern(const bl_gencfg_t *cfg, int role, const bl_key_t *key, uint64_t seed, bl_pattern_t *out);
/* ARP with an explicit duration, used by the live factory's phase_steps adjustment.
 * length must be 240..INT32_MAX ticks, else one bar. Emits the first BL_MAX_NOTES
 * onsets and returns the omitted count. Invalid scale data yields an empty pattern.
 * cfg uses the ARP a/b meanings above; provenance is always BL_GEN_ARP. */
int bl_make_arp(const bl_gencfg_t *cfg, const bl_key_t *key, uint64_t seed, bl_tick_t length, bl_pattern_t *out);
/* The same duration/rate/gate contract, with restrained seeded melodic color on
 * the ARP foundation. Root and local midpoint anchors stay; no automatic evolution.
 * Provenance is BL_GEN_MOTIF. See docs/motif.md for candidate and budget rules. */
int bl_make_motif(const bl_gencfg_t *cfg, const bl_key_t *key, uint64_t seed, bl_tick_t length, bl_pattern_t *out);
/* PatternFactory.makeBassMelody(id:bars:schedule:meter:seed:); a NULL schedule is the pedal */
int bl_make_bass_melody(int bars, const struct bl_sched *sched, uint64_t seed, bl_pattern_t *out);
