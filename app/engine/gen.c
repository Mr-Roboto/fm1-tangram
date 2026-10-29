/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The generators (gen.h). Each draw is where Swift makes it: an argument's draw before the call it is passed to,
 * none from an empty collection's randomElement. Swift's Sets here are positions and indices: one used only for
 * membership is a scan of the notes, one iterated is iterated in the order Swift sorts it. A randomElement of a
 * filtered collection counts the matches, draws the index, then finds that match. The products are 32-bit (a
 * pattern is shorter than 2^31 ticks): the FM-1 wants 64-bit multiplies out of line (felucca-bsp docs/CPU.md). */
#include <stddef.h>
#include "gen.h"
#include "harmony.h"
#include "rng.h"

static const bl_meter_t M = {4, 4};                 /* Meter.fourFour */
static const int8_t STEPS[4] = {-2, -1, 1, 2};      /* the [-2, -1, 1, 2] the moves draw from */

/* the notes of the pattern being made that did not fit (begin() clears it; the make functions return it) */
static int dropped;

/* PatternNote(position:pitch:velocity:gate:accent:), ratchet 1; beyond BL_MAX_NOTES (no template config gets there)
 * a note is dropped and counted */
static void add_note(bl_pattern_t *p, bl_tick_t position, bl_pitch_t pitch, int velocity, bl_tick_t gate, int accent)
{
    if (p->n >= BL_MAX_NOTES) {
        dropped++;
        return;
    }
    p->note[p->n++] = (bl_pnote_t){position, gate, pitch, (uint8_t)velocity, (uint8_t)accent, 1};
}

static bl_pitch_t degree(int32_t d) { return (bl_pitch_t){d, 0, 0}; }

static int occupied(const bl_pattern_t *p, bl_tick_t position)
{
    for (int i = 0; i < p->n; i++)
        if (p->note[i].position == position) return 1;
    return 0;
}

/* step(_:): a 16th */
static int32_t step(void) { return bl_ticks_per_beat(M) / 4; }

/* shortGate(_:_:): 40-60 % of a 16th */
static bl_tick_t short_gate(bl_rng_t *r)
{
    int32_t s = step();
    return bl_rand_in(r, s * 40 / 100, s * 60 / 100);
}

/* lockedTicks(bars:meter:), as membership: beats 1 and 3 of every bar */
static int locked_tick(int bars, bl_tick_t t)
{
    for (int bar = 0; bar < bars; bar++) {
        bl_tick_t base = bar * bl_ticks_per_bar(M);
        if (t == base || (M.beats >= 3 && t == base + 2 * bl_ticks_per_beat(M))) return 1;
    }
    return 0;
}

/* ------------------------------------------------------ the seed procedure --- */
/* seedProcedure(bars:mutations:role:key:meter:rng:) */
static void seed_procedure(int bars, int mutations, int role, bl_rng_t *r, bl_pattern_t *p)
{
    if (role == BL_ROLE_BASS) {
        /* locked.sorted(): the anchors in time order */
        for (int bar = 0; bar < bars; bar++) {
            bl_tick_t base = bar * bl_ticks_per_bar(M);
            add_note(p, base, degree(0), 100, short_gate(r), 0);
            if (M.beats >= 3) add_note(p, base + 2 * bl_ticks_per_beat(M), degree(0), 100, short_gate(r), 0);
        }
        return;
    }
    for (int bar = 0; bar < bars; bar++)
        for (int beat = 0; beat < M.beats; beat++)
            add_note(p, bar * bl_ticks_per_bar(M) + beat * bl_ticks_per_beat(M), degree(0), 100,
                     short_gate(r), 0);
    bl_tick_t length = bars * bl_ticks_per_bar(M);
    int octave_adds = 0;
    for (int m = 0; m < mutations; m++) {
        switch (bl_rand_in(r, 0, 3)) {
        case 0: {   /* move a note off the anchors by 1-2 steps */
            int movable = 0, idx = -1;
            for (int i = 0; i < p->n; i++) movable += !locked_tick(bars, p->note[i].position);
            if (movable == 0) continue;
            int64_t k = bl_rand_index(r, movable);
            for (int i = 0; i < p->n && idx < 0; i++)
                if (!locked_tick(bars, p->note[i].position) && k-- == 0) idx = i;
            bl_tick_t target = p->note[idx].position + STEPS[bl_rand_index(r, 4)] * step();
            if (target < 0 || target >= length || occupied(p, target)) continue;
            p->note[idx].position = target;
            break;
        }
        case 1: {   /* move a degree, never the first sounding note's, within -3...4 */
            bl_tick_t first = p->note[bl_first_sounding(p)].position;
            int candidates = 0, idx = -1;
            for (int i = 0; i < p->n; i++) candidates += p->note[i].position != first;
            if (candidates == 0) continue;
            int64_t k = bl_rand_index(r, candidates);
            for (int i = 0; i < p->n && idx < 0; i++)
                if (p->note[i].position != first && k-- == 0) idx = i;
            int32_t d = p->note[idx].pitch.degree + (int32_t)STEPS[bl_rand_index(r, 4)];
            p->note[idx].pitch.degree = d > 4 ? 4 : d < -3 ? -3 : d;    /* max(-3, min(4, d)) */
            break;
        }
        case 2: {   /* add the root an octave up on a free step, at most twice */
            if (octave_adds >= 2) continue;
            int nfree = 0;
            int32_t steps = (int32_t)length / step();
            bl_tick_t pos = -1;
            for (int32_t s = 0; s < steps; s++) nfree += !occupied(p, s * step());
            if (nfree == 0) continue;
            int64_t k = bl_rand_index(r, nfree);
            for (int32_t s = 0; s < steps && pos < 0; s++)
                if (!occupied(p, s * step()) && k-- == 0) pos = s * step();
            add_note(p, pos, (bl_pitch_t){0, 1, 0}, 95, short_gate(r), 0);
            octave_adds++;
            break;
        }
        default: {  /* toggle an accent */
            int64_t i = bl_rand_index(r, p->n);
            p->note[i].accent = !p->note[i].accent;
            break;
        }
        }
    }
}

/* ------------------------------------------------------- the pulsing bass --- */
/* pulsingBass(accents:meter:rng:): 16ths on the root, a few of them moved off it and accented */
static void pulsing_bass(int accents, bl_rng_t *r, bl_pattern_t *p)
{
    int steps = M.beats * 4;
    for (int k = 0; k < steps; k++) add_note(p, k * step(), degree(0), 72, short_gate(r), 0);
    int count = accents < 4 ? accents : 4;
    count = count > 2 ? count : 2;                  /* max(2, min(4, accents)) */
    uint8_t chosen[BL_MAX_NOTES] = {0};
    int have = 0;
    while (have < count) {                          /* mutable = 1..<steps; a repeat still draws */
        int64_t k = 1 + bl_rand_index(r, steps - 1);
        if (!chosen[k]) have++;
        chosen[k] = 1;
    }
    for (int k = 1; k < steps; k++) {               /* chosen.sorted() */
        if (!chosen[k]) continue;
        switch (bl_rand_in(r, 0, 2)) {
        case 0: p->note[k].pitch.octave = 1; break;
        case 1: p->note[k].pitch.octave = -1; break;
        default: p->note[k].pitch.degree = (int32_t)STEPS[bl_rand_index(r, 4)]; break;
        }
        p->note[k].velocity = 100;
        p->note[k].accent = 1;
    }
}

/* -------------------------------------------------------- the staged pluck --- */
/* stagedPluck's add: a position already taken adds nothing (and draws no gate) */
static void pluck_add(bl_pattern_t *p, bl_rng_t *r, bl_tick_t position, bl_pitch_t pitch, int accent)
{
    if (occupied(p, position)) return;
    add_note(p, position, pitch, 100, short_gate(r), accent);
}

/* stagedPluck(bars:meter:rng:): the root and two syncopated roots per bar, an octave jump, scale fills, a high
 * micro-melody in the last bar, and the accented closing note on the last step */
static void staged_pluck(int bars, bl_rng_t *r, bl_pattern_t *p)
{
    static const int8_t POOL[10] = {2, 3, 5, 6, 7, 9, 10, 11, 13, 14};
    int steps = M.beats * 4;
    bl_tick_t length = bars * bl_ticks_per_bar(M);
    for (int bar = 0; bar < bars; bar++) {
        bl_tick_t start = bar * bl_ticks_per_bar(M);
        pluck_add(p, r, start, degree(0), 0);
        bl_tick_t syncopes[2];
        for (int i = 0; i < 2; i++) {
            syncopes[i] = start + POOL[bl_rand_index(r, 10)] * step();
            pluck_add(p, r, syncopes[i], degree(0), 0);
        }
        /* syncopes.first(where: occupied and != 0); then barIndex == 0 || Bool.random */
        int target = -1;
        for (int i = 0; i < 2 && target < 0; i++)
            if (occupied(p, syncopes[i]) && syncopes[i] != 0) target = i;
        if (target >= 0 && (bar == 0 || bl_rand_bool(r))) {
            for (int i = 0; i < p->n; i++)
                if (p->note[i].position == syncopes[target]) {
                    p->note[i].pitch.octave = 1;
                    break;
                }
        }
        for (int i = 0; i < 2; i++) {
            bl_tick_t pos = start + (int32_t)bl_rand_upto(r, 0, steps) * step();
            pluck_add(p, r, pos, degree((int32_t)bl_rand_in(r, 1, 4)), 0);
        }
        if (bar == bars - 1)
            for (int i = 0; i < 2; i++) {
                bl_tick_t pos = start + (int32_t)bl_rand_upto(r, 0, steps) * step();
                pluck_add(p, r, pos, degree((int32_t)bl_rand_in(r, 4, 7)), 0);
            }
    }
    bl_tick_t last = length - step();
    int n = 0;
    for (int i = 0; i < p->n; i++)                  /* notes.removeAll { position == lastStep } */
        if (p->note[i].position != last) p->note[n++] = p->note[i];
    p->n = (uint8_t)n;
    pluck_add(p, r, last, degree((int32_t)bl_rand_in(r, 5, 6)), 1);
}

/* -------------------------------------------------------- the chord layer --- */
/* chordLayer(gate:key:rng:): 20 % the home root, else 1-12 semitones up, quantized, stacked +4/+7/+12 and
 * quantized again; a tone that resolves as an earlier one is left out */
static void chord_layer(bl_tick_t gate, const bl_key_t *key, bl_rng_t *r, bl_pattern_t *p)
{
    static const int8_t STACK[4] = {0, 4, 7, 12};
    const int32_t ref_c = 60;
    int32_t root_midi = f64_lt(bl_rand_unit(r), F64(0x3FC999999999999A))        /* 0.2 */
                            ? ref_c
                            : ref_c + (int32_t)bl_rand_in(r, 1, 12);
    int32_t root = bl_resolve(key, bl_quantize(key, root_midi, ref_c), ref_c);
    int32_t seen[4];
    int nseen = 0;
    for (int i = 0; i < 4; i++) {
        bl_pitch_t tone = bl_quantize(key, root + STACK[i], ref_c);
        int32_t resolved = bl_resolve(key, tone, ref_c), dup = 0;
        for (int j = 0; j < nseen; j++) dup |= seen[j] == resolved;
        if (dup) continue;
        seen[nseen++] = resolved;
        add_note(p, 0, tone, 84, gate, 0);
    }
}

/* ------------------------------------------------------------ the factory --- */
static void begin(bl_pattern_t *p, int generator, bl_tick_t length, uint64_t seed)
{
    p->length = length;
    p->start_offset = 0;
    p->n = 0;
    p->generator = (uint8_t)generator;
    p->locked = 0;
    p->remaining = 4;           /* .repeating(remaining: 4) */
    p->seed = seed;
    dropped = 0;
}

/* ------------------------------------------------------- Tangram ARP (UP) --- */
/* Choose unique degrees nearest the interval roles [3,4], 7, [10,11], with
 * lower semitones winning ties. Root is fixed; sort the result for UP order.
 * This uses musical intervals rather than assuming seven-note scale degrees. */
static int arp_chord(const bl_scale_t *scale, int chord[4])
{
    static const int8_t LOW[3] = {3, 7, 10}, HIGH[3] = {4, 7, 11};
    if (scale->count < 4 || scale->count > 12 || scale->off[0] != 0) return 0;
    for (int d = 1; d < scale->count; d++)
        if (scale->off[d] <= scale->off[d - 1] || scale->off[d] > 11) return 0;
    chord[0] = 0;
    for (int role = 0; role < 3; role++) {
        int best = -1, distance = 12;
        for (int d = 1; d < scale->count; d++) {
            int used = 0, semitone = scale->off[d];
            for (int j = 0; j <= role; j++) used |= chord[j] == d;
            if (used) continue;
            int delta = semitone < LOW[role] ? LOW[role] - semitone :
                        semitone > HIGH[role] ? semitone - HIGH[role] : 0;
            if (delta < distance) best = d, distance = delta;
        }
        chord[role + 1] = best;
    }
    for (int i = 1; i < 4; i++) {
        int d = chord[i], j = i;
        while (j > 0 && chord[j - 1] > d) {
            chord[j] = chord[j - 1];
            j--;
        }
        chord[j] = d;
    }
    return 4;
}

int bl_make_arp(const bl_gencfg_t *cfg, const bl_key_t *key, uint64_t seed, bl_tick_t length, bl_pattern_t *out)
{
    int32_t spacing = cfg->a == BL_PPQN / 4 ? BL_PPQN / 4 : BL_PPQN / 2;
    int32_t gate = cfg->b > 0 && cfg->b < spacing ? cfg->b : spacing / 2;
    if (length < BL_PPQN / 4 || length > INT32_MAX) length = BL_TICKS_PER_BAR;
    begin(out, BL_GEN_ARP, length, seed);
    int chord[4];
    int tones = arp_chord(&key->scale, chord);
    if (!tones) return 0;
    /* Duration determines the available onset slots. Occupancy is straight for
     * now; the pitch cycle advances once per sounding note, not per bar. */
    int32_t slots = (int32_t)((length - 1) / spacing + 1);
    int32_t kept = slots < BL_MAX_NOTES ? slots : BL_MAX_NOTES;
    for (int32_t i = 0; i < kept; i++) {
        int cycle = i % (2 * tones);
        int d = chord[cycle % tones] + (cycle / tones) * key->scale.count;
        add_note(out, i * spacing, degree(d), 100, gate, 0);
    }
    dropped = slots - kept;
    return dropped;
}

/* ----------------------------------------------------- Tangram stable MOTIF --- */
/* Non-chord scale neighbors inside the surrounding ascending contour. The
 * perfect-fourth cap is deliberately conservative, including in sparse scales.
 * Wraps and phrase ends are left alone; this is not loop-boundary correction. */
static int motif_neighbors(const bl_key_t *key, int before, int center, int after, const int chord[4], int out[2])
{
    int count = key->scale.count, n = 0;
    int32_t previous = bl_resolve(key, degree(before), 0);
    int32_t next = bl_resolve(key, degree(after), 0);
    int32_t original = bl_resolve(key, degree(center), 0);
    if (previous >= original || original >= next) return 0;
    for (int delta = -1; delta <= 1; delta += 2) {
        int d = center + delta, is_chord = 0;
        if (d < 0 || d >= 2 * count) continue;
        for (int j = 0; j < 4; j++) is_chord |= d % count == chord[j];
        if (is_chord) continue;
        int32_t midi = bl_resolve(key, degree(d), 0);
        if (midi > previous && midi < next && midi - previous <= 5 && next - midi <= 5)
            out[n++] = d;
    }
    return n;
}

static int motif_candidates(const bl_key_t *key, const bl_pattern_t *p, int i, const int chord[4], int out[2])
{
    return motif_neighbors(key, p->note[i - 1].pitch.degree, p->note[i].pitch.degree,
                           p->note[i + 1].pitch.degree, chord, out);
}

int bl_make_motif(const bl_gencfg_t *cfg, const bl_key_t *key, uint64_t seed, bl_tick_t length, bl_pattern_t *out)
{
    int omitted = bl_make_arp(cfg, key, seed, length, out);
    out->generator = BL_GEN_MOTIF;
    int budget = out->n / 8;
    if (!budget) return omitted;        /* empty, very short, or malformed scale */
    int chord[4], midpoint = out->n;
    for (int j = 0; j < 4; j++) chord[j] = out->note[j].pitch.degree;
    for (int i = 1; i < out->n; i++)
        if (out->note[i].position >= (out->length + 1) / 2) {
            midpoint = i;
            break;
        }
    bl_rng_t r;
    bl_rng_seed(&r, seed);
    uint8_t blocked[BL_MAX_NOTES] = {0};
    for (int change = 0; change < budget; change++) {
        uint8_t eligible[BL_MAX_NOTES];
        int n = 0, candidates[2];
        for (int i = 1; i + 1 < out->n; i++)
            if (i != midpoint && !blocked[i] && motif_candidates(key, out, i, chord, candidates))
                eligible[n++] = (uint8_t)i;
        if (!n) break;                 /* never force an unsuitable replacement */
        int i = eligible[bl_rand_index(&r, n)];
        int nc = motif_candidates(key, out, i, chord, candidates);
        out->note[i].pitch.degree = candidates[bl_rand_index(&r, nc)];
        /* Each changed tone remains surrounded by its original chord tones. */
        blocked[i - 1] = blocked[i] = blocked[i + 1] = 1;
    }
    return omitted;
}

/* --------------------------------------------------- bounded MOTIF evolution --- */
void bl_motif_evolution_init(bl_motif_evo_t *e, const bl_key_t *key, uint64_t seed, const char *voice_id)
{
    e->interval_rng = bl_seed_derive(seed, "motif-interval", voice_id, (const char *)NULL);
    e->melody_rng = bl_seed_derive(seed, "motif-melody", voice_id, (const char *)NULL);
    e->cycles_left = 4;
    int chord[4];
    e->scale_count = arp_chord(&key->scale, chord) ? key->scale.count : 0;
    for (int i = 0; i < 4; i++) e->chord[i] = e->scale_count ? (uint8_t)chord[i] : 0;
}

static int motif_base(const bl_motif_evo_t *e, int i)
{
    return e->chord[i % 4] + ((i % 8) / 4) * e->scale_count;
}

static int evolution_candidates(const bl_key_t *key, const bl_pattern_t *p, const bl_motif_evo_t *e, int i, int out[2])
{
    if (!i || i + 1 >= p->n) return 0;
    /* First onset at/after the local midpoint. */
    if (p->note[i].position >= (p->length + 1) / 2 && p->note[i - 1].position < (p->length + 1) / 2) return 0;
    int chord[4];
    for (int j = 0; j < 4; j++) chord[j] = e->chord[j];
    return motif_neighbors(key, motif_base(e, i - 1), motif_base(e, i), motif_base(e, i + 1), chord, out);
}

/* Refuse altered grids, unsupported scale-frame changes, or invalid incoming
 * material rather than compound drift after an external pattern/key action. */
static int evolution_frame(const bl_pattern_t *p, const bl_motif_evo_t *e, const bl_key_t *key, uint8_t slots[8])
{
    int chord[4], changes = 0;
    if (p->n < 8 || p->n > BL_MAX_NOTES || e->scale_count != key->scale.count || !arp_chord(&key->scale, chord)) return -1;
    for (int j = 0; j < 4; j++) if (e->chord[j] != chord[j]) return -1;
    if (p->note[1].position != 240 && p->note[1].position != 480) return -1;
    int32_t spacing = (int32_t)p->note[1].position; /* keep onset products 32-bit (FM-1 CPU.md) */
    bl_tick_t expected = (p->length - 1) / spacing + 1;
    if (p->n != (expected < BL_MAX_NOTES ? expected : BL_MAX_NOTES)) return -1;
    for (int i = 0; i < p->n; i++) {
        if (p->note[i].position != i * spacing || p->note[i].pitch.octave || p->note[i].pitch.accidental) return -1;
        if (p->note[i].pitch.degree == motif_base(e, i)) continue;
        int candidates[2], n = evolution_candidates(key, p, e, i, candidates), valid = 0;
        for (int j = 0; j < n; j++) valid |= candidates[j] == p->note[i].pitch.degree;
        if (!valid || changes >= p->n / 8 || (changes && slots[changes - 1] + 1 == i)) return -1;
        slots[changes++] = (uint8_t)i;
    }
    return changes;
}

static int evolve_melody(bl_pattern_t *p, const bl_motif_evo_t *e, const bl_key_t *key, bl_rng_t *r)
{
    uint8_t slots[8];
    int changes = evolution_frame(p, e, key, slots);
    if (changes < 0) return 0;
    int slot = (int)bl_rand_index(r, p->n / 8);
    int old = slot < changes ? slots[slot] : -1;
    int old_degree = old >= 0 ? p->note[old].pitch.degree : 0;
    /* A vacant variation slot may be filled. Occupied slots can restore,
     * replace, or relocate; all selection is against the immutable foundation. */
    int operation = old >= 0 ? (int)bl_rand_index(r, 3) : 2;
    if (old >= 0) p->note[old].pitch.degree = motif_base(e, old);
    if (operation == 0) return 1;        /* restore one substitution */
    uint8_t positions[BL_MAX_NOTES];
    int npositions = 0;
    for (int i = 1; i + 1 < p->n; i++) {
        if ((operation == 1 && i != old) || (operation == 2 && i == old)) continue;
        if (p->note[i].pitch.degree != motif_base(e, i) ||
            p->note[i - 1].pitch.degree != motif_base(e, i - 1) ||
            p->note[i + 1].pitch.degree != motif_base(e, i + 1)) continue;
        int candidates[2], n = evolution_candidates(key, p, e, i, candidates), kept = 0;
        for (int j = 0; j < n; j++)
            if (i != old || candidates[j] != old_degree) kept++;
        if (kept) {
            positions[npositions++] = (uint8_t)i;
        }
    }
    if (!npositions) {
        if (old >= 0) p->note[old].pitch.degree = old_degree;
        return 0;
    }
    int i = positions[bl_rand_index(r, npositions)], candidates[2], kept = 0;
    int n = evolution_candidates(key, p, e, i, candidates);
    for (int j = 0; j < n; j++)
        if (i != old || candidates[j] != old_degree) candidates[kept++] = candidates[j];
    p->note[i].pitch.degree = candidates[bl_rand_index(r, kept)];
    return 1;
}

int bl_motif_evolution_boundary(bl_pattern_t *p, bl_motif_evo_t *e, const bl_key_t *key)
{
    if (p->generator != BL_GEN_MOTIF || p->locked || !e->cycles_left) return 0;
    if (--e->cycles_left) return 0;
    bl_rng_t interval = {e->interval_rng}, melody = {e->melody_rng};
    e->cycles_left = (uint8_t)(2 * (1 + bl_rand_index(&interval, 4)));
    int changed = evolve_melody(p, e, key, &melody);
    e->interval_rng = interval.state;
    e->melody_rng = melody.state;
    return changed ? 2 : 1;
}

/* PatternFactory.make */
int bl_make_pattern(const bl_gencfg_t *cfg, int role, const bl_key_t *key, uint64_t seed, bl_pattern_t *out)
{
    bl_rng_t r;
    bl_rng_seed(&r, seed);
    switch (cfg->kind) {
    case BL_GEN_SEED_PROCEDURE:
        begin(out, cfg->kind, cfg->a * bl_ticks_per_bar(M), seed);
        seed_procedure(cfg->a, cfg->b, role, &r, out);
        break;
    case BL_GEN_PULSING_BASS:
        begin(out, cfg->kind, bl_ticks_per_bar(M), seed);
        pulsing_bass(cfg->a, &r, out);
        break;
    case BL_GEN_STAGED_PLUCK:
        begin(out, cfg->kind, cfg->a * bl_ticks_per_bar(M), seed);
        staged_pluck(cfg->a, &r, out);
        break;
    case BL_GEN_BASS_MELODY:
        return bl_make_bass_melody(cfg->a, NULL, seed, out);
    case BL_GEN_CHORD_LAYER:
        begin(out, cfg->kind, cfg->a * bl_ticks_per_bar(M), seed);
        chord_layer(out->length, key, &r, out);
        break;
    case BL_GEN_HEARTBEAT:      /* the quarter-note kick */
        begin(out, cfg->kind, bl_ticks_per_bar(M), seed);
        for (int b = 0; b < M.beats; b++)
            add_note(out, b * bl_ticks_per_beat(M), degree(0), 92, bl_ticks_per_beat(M) / 2, 0);
        break;
    case BL_GEN_DRONE:          /* root and fifth for the whole loop */
        begin(out, cfg->kind, cfg->a * bl_ticks_per_bar(M), seed);
        add_note(out, 0, degree(0), 64, out->length, 0);
        add_note(out, 0, degree(4), 60, out->length, 0);
        break;
    case BL_GEN_ARP:
        return bl_make_arp(cfg, key, seed, bl_ticks_per_bar(M), out);
    case BL_GEN_MOTIF:
        return bl_make_motif(cfg, key, seed, bl_ticks_per_bar(M), out);
    default:                    /* not a ported generator: an empty bar */
        begin(out, cfg->kind, bl_ticks_per_bar(M), seed);
        break;
    }
    return dropped;
}

/* ------------------------------------------------------- the bass melody --- */
static int32_t offset_at(const struct bl_sched *s, int32_t bar) { return s ? bl_sched_offset(s, bar) : 0; }

/* PatternFactory.makeBassMelody: each bar's root on beat 1, then a seeded walk of passing tones (steps of at most
 * 2 degrees) toward the next bar's root, some pushed off the beat, some an octave down */
int bl_make_bass_melody(int bars, const struct bl_sched *sched, uint64_t seed, bl_pattern_t *out)
{
    static const int8_t HOLD[4] = {0, 1, 1, 2};
    bl_rng_t r;
    bl_rng_seed(&r, seed);
    begin(out, BL_GEN_BASS_MELODY, bars * bl_ticks_per_bar(M), seed);
    int32_t beat_ticks = bl_ticks_per_beat(M);
    for (int bar = 0; bar < bars; bar++) {
        int32_t root = offset_at(sched, bar), next = offset_at(sched, (bar + 1) % bars), delta = next - root;
        int32_t max_slots = M.beats - 1, adelta = delta < 0 ? -delta : delta;
        int32_t min_needed = delta == 0 ? 0 : (adelta + 1) / 2 - 1 > 0 ? (adelta + 1) / 2 - 1 : 0;
        int32_t count;
        if (delta == 0) {
            count = HOLD[bl_rand_index(&r, 4)];                         /* hold or dip */
        } else {
            int32_t room = max_slots - min_needed > 0 ? max_slots - min_needed : 0;
            count = min_needed + (int32_t)bl_rand_in(&r, 0, room < 2 ? room : 2);
        }
        if (count > max_slots) count = max_slots;

        bl_tick_t start = bar * bl_ticks_per_bar(M);
        int32_t held = M.beats - count > 1 ? M.beats - count : 1;
        add_note(out, start, degree(root), 100, held * beat_ticks, 1);

        int32_t degrees[4];
        int nd = 0;
        if (delta == 0) {
            if (count == 1) {
                degrees[nd++] = root - (int32_t)bl_rand_in(&r, 1, 2);
            } else if (count == 2) {
                int down = bl_rand_bool(&r);
                degrees[nd++] = down ? root - 1 : root - 2;
                degrees[nd++] = down ? root - 2 : root - 1;
            }
        } else if (count > 0) {
            int32_t current = root, remaining = delta;
            for (int slot = 0; slot < count; slot++) {
                int32_t gaps_after = count - slot, g = 0;             /* the final gap included */
                int nc = 0;
                for (int i = 0; i < 4; i++) {
                    int32_t left = remaining - (int32_t)STEPS[i];
                    nc += (left < 0 ? -left : left) <= 2 * gaps_after;
                }
                if (nc > 0) {
                    int64_t k = bl_rand_index(&r, nc);
                    for (int i = 0;; i++) {
                        int32_t left = remaining - (int32_t)STEPS[i];
                        if ((left < 0 ? -left : left) <= 2 * gaps_after && k-- == 0) {
                            g = (int32_t)STEPS[i];
                            break;
                        }
                    }
                } else {
                    g = remaining > 0 ? (remaining < 2 ? remaining : 2) : (remaining > -2 ? remaining : -2);
                }
                current += g;
                remaining -= g;
                degrees[nd++] = current;
            }
        }

        for (int k = 0; k < nd; k++) {
            bl_tick_t position = start + (M.beats - nd + k) * beat_ticks, gate = beat_ticks;
            if (f64_lt(bl_rand_unit(&r), F64(0x3FC999999999999A))) {   /* 0.2: the off-beat push */
                position += beat_ticks / 2;
                gate = beat_ticks / 2;
            }
            bl_pitch_t pitch = degree(degrees[k]);
            if (f64_lt(bl_rand_unit(&r), F64(0x3FC3333333333333)))     /* 0.15: the octave dip */
                pitch.octave = -1;
            add_note(out, position, pitch, 100, gate, 0);
        }
    }
    return dropped;
}
