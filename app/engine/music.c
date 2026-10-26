/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Scales, keys and degree pitch (music.h). */
#include <stdlib.h>
#include "music.h"

const char *const BL_MODE_RAW[BL_NMODES] = {
    "naturalMinor", "dorian", "phrygian", "mixolydian", "lydian",
    "lydianDominant", "harmonicMinor", "minorPentatonic", "locrian", "major",
};

/* Scale.preset, in ModeID's order */
static const struct {
    uint8_t count;
    int8_t off[7];
} PRESET[BL_NMODES] = {
    {7, {0, 2, 3, 5, 7, 8, 10}},    /* naturalMinor */
    {7, {0, 2, 3, 5, 7, 9, 10}},    /* dorian */
    {7, {0, 1, 3, 5, 7, 8, 10}},    /* phrygian */
    {7, {0, 2, 4, 5, 7, 9, 10}},    /* mixolydian */
    {7, {0, 2, 4, 6, 7, 9, 11}},    /* lydian */
    {7, {0, 2, 4, 6, 7, 9, 10}},    /* lydianDominant */
    {7, {0, 2, 3, 5, 7, 8, 11}},    /* harmonicMinor */
    {5, {0, 3, 5, 7, 10}},          /* minorPentatonic */
    {7, {0, 1, 3, 5, 6, 8, 10}},    /* locrian */
    {7, {0, 2, 4, 5, 7, 9, 11}},    /* major */
};

void bl_scale_preset(int mode, bl_scale_t *out)
{
    out->mode = (uint8_t)mode;
    out->count = PRESET[mode].count;
    for (int i = 0; i < 12; i++) out->off[i] = i < out->count ? PRESET[mode].off[i] : 0;
}

int64_t bl_floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

int64_t bl_floor_mod(int64_t a, int64_t b)
{
    int64_t r = a % b;
    return (r != 0 && (r < 0) != (b < 0)) ? r + b : r;
}

int32_t bl_resolve(const bl_key_t *k, bl_pitch_t p, int32_t ref_c)
{
    int32_t d = (int32_t)bl_floor_mod(p.degree, k->scale.count);
    int32_t carry = (int32_t)bl_floor_div(p.degree, k->scale.count);
    return ref_c + 12 * (p.octave + carry) + k->root + k->scale.off[d] + p.accidental;
}

bl_pitch_t bl_quantize(const bl_key_t *k, int32_t midi, int32_t ref_c)
{
    int32_t center = (int32_t)bl_floor_div(midi - ref_c - k->root, 12);
    bl_pitch_t best = {0, 0, 0};
    int32_t best_r = 0;
    int have = 0;
    for (int32_t octave = center - 1; octave <= center + 1; octave++) {
        for (int32_t d = 0; d < k->scale.count; d++) {
            bl_pitch_t p = {d, octave, 0};
            int32_t r = bl_resolve(k, p, ref_c);
            /* strictly closer wins; a tie goes to the lower pitch */
            if (!have || abs(r - midi) < abs(best_r - midi) || (abs(r - midi) == abs(best_r - midi) && r < best_r)) {
                best = p;
                best_r = r;
                have = 1;
            }
        }
    }
    return best;
}

uint8_t bl_fold(uint8_t low, uint8_t high, int32_t note)
{
    while (note > high) note -= 12;
    while (note < low) note += 12;
    return (uint8_t)note;
}
