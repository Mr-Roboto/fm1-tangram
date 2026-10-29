/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Patterns as Berlinan has them (Pattern.swift, Phasing.swift, PatternOps.swift): a loop of notes in degree space,
 * where a loop's notes sound on the global tick grid, the length change of phasing, and the pattern ops of the
 * arrangement (density cap, thin to roots, move the accents). Not ported, as no template reaches them: ties, gate
 * cycles, duplicate-and-tweak. */
#pragma once
#include <stdint.h>
#include "bl.h"
#include "f64.h"
#include "music.h"
#include "rng.h"

/* PatternNote (tie: always nil in the templates' material) */
typedef struct {
    bl_tick_t position, gate;   /* onset within the loop, 0..<length; gate > 0 */
    bl_pitch_t pitch;
    uint8_t velocity, accent, ratchet;
} bl_pnote_t;

/* Pitch traversal only; zero-initialized configuration retains UP. */
enum { BL_DIR_UP, BL_DIR_DOWN, BL_DIR_M_DOWN, BL_DIR_PENDULUM, BL_DIR_M_PENDULUM, BL_NDIRS };

/* Playback-only rhythm; zero preserves every source onset and accent. */
enum { BL_GROOVE_STRAIGHT, BL_GROOVE_PULSE_8, BL_GROOVE_DRIVE_16, BL_GROOVE_BROKEN_8, BL_NGROOVES };

/* Pattern: generator and seed are its provenance (a BL_GEN_ of gen.h), remaining its lifecycle */
typedef struct {
    bl_tick_t length, start_offset;
    uint8_t n, generator, locked;
    uint8_t direction, direction_cursor; /* ARP/MOTIF source traversal at the local loop's first onset */
    uint8_t groove;             /* ARP/MOTIF playback mask, never edits source notes */
    uint8_t pitch_phase;        /* offset in ordered traversal positions, not ticks */
    int16_t remaining;          /* .repeating(remaining:); -1 = .established */
    uint64_t seed;
    bl_pnote_t note[BL_MAX_NOTES];
} bl_pattern_t;

/* MOTIF's bounded runtime state, separate from the generic pattern lifecycle.
 * Four degree indices + scale count reconstruct the original eight-tone ARP
 * cycle; the current substitutions already live in pattern.note[]. */
typedef struct {
    uint64_t interval_rng, melody_rng;
    uint8_t cycles_left, scale_count, chord[4];
} bl_motif_evo_t;

/* one onset of occurrences(in:): the global tick and the note's index */
typedef struct {
    bl_tick_t at;
    uint8_t index;
} bl_occ_t;

/* Pattern.occurrences(in: lo..<hi): sorted by at, then by the note's position (stable: equal ones in note order).
 * Returns the number of onsets in the window and writes the first max of them (as snprintf: a return above max
 * means the buffer was too small and the rest are missing) */
int bl_occurrences(const bl_pattern_t *p, bl_tick_t lo, bl_tick_t hi, bl_occ_t *out, int max);
/* Pattern.hasOnset(atGlobalTick:) */
int bl_has_onset(const bl_pattern_t *p, bl_tick_t global);
/* Pattern.firstSoundingNote: the index of the first note with the least position, -1 when there is none */
int bl_first_sounding(const bl_pattern_t *p);

/* Map a local onset index to a stored pitch without moving its timing/gate or
 * altering MOTIF's protected foundation. Live rendering passes completed_loops=0
 * and advances the cursor once per completed local loop, never per render call.
 * Direct rendering passes the loop ordinal relative to its cursor's origin.
 * Non-ARP/MOTIF callers leave these fields zero. */
bl_pitch_t bl_direction_pitch(const bl_pattern_t *p, int onset, bl_tick_t completed_loops);
void bl_direction_advance(bl_pattern_t *p);
int bl_direction_valid(const bl_pattern_t *p);
/* Available source positions, at least one even for an empty pattern. PHASE
 * range is 0..count-1; the render helper safely folds after source thinning. */
int bl_pitch_phase_count(const bl_pattern_t *p);

/* Whether a source onset sounds. Local tick position supplies the groove phase;
 * active groove accents are ORed into *accent, leaving source accents intact. */
int bl_groove_onset(const bl_pattern_t *p, bl_tick_t position, uint8_t *accent);

/* Pattern.withLength(_:), in place: the notes at or beyond the new end are dropped. new_len <= 0 (Swift traps)
 * leaves the pattern unchanged and returns -1; else 0 */
int bl_with_length(bl_pattern_t *p, bl_tick_t new_len);

/* PatternOps.densityCap(_:density:meter:) in 4/4; out may be in */
void bl_density_cap(const bl_pattern_t *in, f64 density, bl_pattern_t *out);
/* PatternOps.thinToRoots(_:), in place */
void bl_thin_to_roots(bl_pattern_t *p);
/* PatternOps.moveAccents(_:seed:), in place */
void bl_move_accents(bl_pattern_t *p, uint64_t seed);
/* the same on a generator already running: Legitimizer.mutate's accent move (step 3) is this code */
void bl_move_accents_with(bl_pattern_t *p, bl_rng_t *r);
