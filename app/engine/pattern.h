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

/* PatternNote (tie: always nil in the templates' material) */
typedef struct {
    bl_tick_t position, gate;   /* onset within the loop, 0..<length; gate > 0 */
    bl_pitch_t pitch;
    uint8_t velocity, accent, ratchet;
} bl_pnote_t;

/* Pattern: generator and seed are its provenance (a BL_GEN_ of gen.h), remaining its lifecycle */
typedef struct {
    bl_tick_t length, start_offset;
    uint8_t n, generator, locked;
    int16_t remaining;          /* .repeating(remaining:); -1 = .established */
    uint64_t seed;
    bl_pnote_t note[BL_MAX_NOTES];
} bl_pattern_t;

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

/* Pattern.withLength(_:), in place: the notes at or beyond the new end are dropped. new_len <= 0 (Swift traps)
 * leaves the pattern unchanged and returns -1; else 0 */
int bl_with_length(bl_pattern_t *p, bl_tick_t new_len);

/* PatternOps.densityCap(_:density:meter:) in 4/4; out may be in */
void bl_density_cap(const bl_pattern_t *in, f64 density, bl_pattern_t *out);
/* PatternOps.thinToRoots(_:), in place */
void bl_thin_to_roots(bl_pattern_t *p);
/* PatternOps.moveAccents(_:seed:), in place */
void bl_move_accents(bl_pattern_t *p, uint64_t seed);
