/* SPDX-License-Identifier: GPL-2.0-or-later */
/* TGM-001A: independent musical expectations, in Tangram ID order. */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "music.h"
#include "test.h"

static const struct {
    int id, count;
    const char *raw;
    int8_t off[7];
} EXPECT[23] = {
    {BL_MODE_NATURAL_MINOR, 7, "naturalMinor", {0,2,3,5,7,8,10}},
    {BL_MODE_DORIAN, 7, "dorian", {0,2,3,5,7,9,10}},
    {BL_MODE_PHRYGIAN, 7, "phrygian", {0,1,3,5,7,8,10}},
    {BL_MODE_MIXOLYDIAN, 7, "mixolydian", {0,2,4,5,7,9,10}},
    {BL_MODE_LYDIAN, 7, "lydian", {0,2,4,6,7,9,11}},
    {BL_MODE_LYDIAN_DOMINANT, 7, "lydianDominant", {0,2,4,6,7,9,10}},
    {BL_MODE_HARMONIC_MINOR, 7, "harmonicMinor", {0,2,3,5,7,8,11}},
    {BL_MODE_MINOR_PENTATONIC, 5, "minorPentatonic", {0,3,5,7,10}},
    {BL_MODE_LOCRIAN, 7, "locrian", {0,1,3,5,6,8,10}},
    {BL_MODE_MAJOR, 7, "major", {0,2,4,5,7,9,11}},
    {BL_MODE_PMAJ, 5, "PMAJ", {0,2,4,7,9}},
    {BL_MODE_EGYP, 5, "EGYP", {0,2,5,7,10}},
    {BL_MODE_HIRA, 5, "HIRA", {0,2,3,7,8}},
    {BL_MODE_MELM, 7, "MELM", {0,2,3,5,7,9,11}},
    {BL_MODE_HMAJ, 7, "HMAJ", {0,2,4,5,7,8,11}},
    {BL_MODE_ALTR, 7, "ALTR", {0,1,3,4,6,8,10}},
    {BL_MODE_GYPS, 7, "GYPS", {0,2,3,6,7,8,11}},
    {BL_MODE_ROMN, 7, "ROMN", {0,2,3,6,7,9,10}},
    {BL_MODE_BHAI, 7, "BHAI", {0,1,4,5,7,8,11}},
    {BL_MODE_MARV, 7, "MARV", {0,1,4,6,7,9,11}},
    {BL_MODE_PURV, 7, "PURV", {0,1,4,6,7,8,11}},
    {BL_MODE_TODI, 7, "TODI", {0,1,3,6,7,8,11}},
    {BL_MODE_ARAB, 7, "ARAB", {0,2,4,5,6,8,10}},
};

int main(void)
{
    CHECK(BL_NMODES == 23, "23 modes, got %d", BL_NMODES);
    for (int m = 0; m < 23; m++) {
        CHECK(EXPECT[m].id == m, "scale ID %d changed to %d", m, EXPECT[m].id);
        bl_key_t k = {0, {0}};
        bl_scale_preset(m, &k.scale);
        CHECK(k.scale.mode == m && k.scale.count == EXPECT[m].count, "mode/count %d", m);
        CHECK(BL_MODE_RAW[m] && !strcmp(BL_MODE_RAW[m], EXPECT[m].raw), "raw name %d", m);
        for (int d = 0; d < 12; d++)
            CHECK(k.scale.off[d] == (d < EXPECT[m].count ? EXPECT[m].off[d] : 0), "offset %d/%d", m, d);
        for (int root = 0; root < 12; root++) {
            k.root = root;
            for (int oct = -3; oct <= 3; oct++) {
                for (int d = 0; d < EXPECT[m].count; d++) {
                    int note = 60 + root + 12 * oct + EXPECT[m].off[d];
                    bl_pitch_t p = {d, oct, 0};
                    CHECK(bl_resolve(&k, p, 60) == note, "resolve %d/%d/%d/%d", m, root, oct, d);
                    p.degree += EXPECT[m].count;
                    CHECK(bl_resolve(&k, p, 60) == note + 12, "degree carry %d/%d", m, root);
                    p.degree -= 2 * EXPECT[m].count;
                    p.accidental = -1;
                    CHECK(bl_resolve(&k, p, 60) == note - 13, "negative degree/accidental %d/%d", m, root);
                }
            }
            /* Brute-force oracle in absolute semitones, independent of degree resolution.
             * Include pitches below reference C, MIDI endpoints, and all ties. */
            for (int midi = 0; midi <= 127; midi++) {
                int best = 0, distance = INT_MAX;
                for (int oct = -7; oct <= 7; oct++) {
                    for (int d = 0; d < EXPECT[m].count; d++) {
                        int n = 60 + root + 12 * oct + EXPECT[m].off[d];
                        int delta = abs(n - midi);
                        if (delta < distance || (delta == distance && n < best)) {
                            best = n;
                            distance = delta;
                        }
                    }
                }
                bl_pitch_t p = bl_quantize(&k, midi, 60);
                CHECK(p.degree >= 0 && p.degree < EXPECT[m].count && p.accidental == 0,
                      "quantized degree %d/%d/%d", m, root, midi);
                CHECK(bl_resolve(&k, p, 60) == best, "nearest tone/tie %d/%d/%d", m, root, midi);
            }
        }
    }
    printf(fails ? "scales: %d FAILED\n" : "scales: ok (23 modes, 12 roots, MIDI 0..127)\n", fails);
    return fails != 0;
}
