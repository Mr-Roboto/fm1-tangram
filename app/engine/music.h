/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Scales, keys and degree pitch as Berlinan has them (Tonality.swift, DegreePitch.swift, RegisterWindow.swift): the
 * ten preset modes, a key's root and scale, a note as scale degree + octave + accidental, resolving it to MIDI,
 * quantizing MIDI into the key, and the octave fold of a voice's register window. */
#pragma once
#include <stdint.h>

/* ModeID's order (custom has no preset and is not here) */
enum {
    BL_MODE_NATURAL_MINOR, BL_MODE_DORIAN, BL_MODE_PHRYGIAN, BL_MODE_MIXOLYDIAN, BL_MODE_LYDIAN,
    BL_MODE_LYDIAN_DOMINANT, BL_MODE_HARMONIC_MINOR, BL_MODE_MINOR_PENTATONIC, BL_MODE_LOCRIAN, BL_MODE_MAJOR,
    BL_NMODES
};

/* ModeID's raw values, as the saved files and the screen spell them */
extern const char *const BL_MODE_RAW[BL_NMODES];

typedef struct {
    uint8_t mode, count;
    int8_t off[12];             /* semitones above the root, ascending, within one octave */
} bl_scale_t;

typedef struct {
    int32_t root;               /* pitch class 0..11 */
    bl_scale_t scale;
} bl_key_t;

/* DegreePitch */
typedef struct {
    int32_t degree, octave, accidental;
} bl_pitch_t;

void bl_scale_preset(int mode, bl_scale_t *out);

/* floorDiv, floorMod: Swift's / and % truncate toward zero */
int64_t bl_floor_div(int64_t a, int64_t b);
int64_t bl_floor_mod(int64_t a, int64_t b);

/* Key.resolve(_:referenceC:): the only place degrees become MIDI numbers */
int32_t bl_resolve(const bl_key_t *k, bl_pitch_t p, int32_t ref_c);
/* Key.quantize(midi:referenceC:): the nearest scale tone; a tie goes down */
bl_pitch_t bl_quantize(const bl_key_t *k, int32_t midi, int32_t ref_c);

/* RegisterWindow.fold: by octaves into low...high */
uint8_t bl_fold(uint8_t low, uint8_t high, int32_t note);
