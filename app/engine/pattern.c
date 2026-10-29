/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Patterns, the loop cursor and the pattern ops (pattern.h). Swift's sorts are stable; so are these (insertion
 * sorts: a pattern has at most 64 notes). */
#include "pattern.h"
#include "rng.h"
#include "gen.h"

int bl_groove_onset(const bl_pattern_t *p, bl_tick_t position, uint8_t *accent)
{
    if ((p->generator != BL_GEN_ARP && p->generator != BL_GEN_MOTIF) || position < 0) return 1;
    /* Tick positions survive direction mapping and density filtering. Use them
     * rather than compacted note indices so silent positions keep their phase. */
    switch (p->groove) {
    case BL_GROOVE_PULSE_8:
        if (bl_floor_mod(position, BL_PPQN / 2)) return 0;
        if (!bl_floor_mod(position, 2 * BL_PPQN)) *accent = 1;
        return 1;
    case BL_GROOVE_DRIVE_16:
        if (!bl_floor_mod(position, BL_PPQN)) *accent = 1;
        return 1;
    case BL_GROOVE_BROKEN_8: {
        if (bl_floor_mod(position, BL_PPQN / 2)) return 0;
        int slot = (int)bl_floor_mod(bl_floor_div(position, BL_PPQN / 2), 8);
        if (!(0xADu & (1u << slot))) return 0; /* PLAY REST PLAY PLAY REST PLAY REST PLAY */
        if (0x89u & (1u << slot)) *accent = 1; /* active slots 1, 4, 8 */
        return 1;
    }
    default: return 1;          /* STRAIGHT, including safe invalid-value fallback */
    }
}

/* ARP has one ordered, two-octave chord cycle (eight tones). MOTIF uses
 * its complete stored phrase so color in either half remains available.
 * A short pattern supplies only the tones actually present. */
static int direction_tones(const bl_pattern_t *p)
{
    return p->generator == BL_GEN_ARP && p->n > 8 ? 8 : p->n;
}

static int direction_period(const bl_pattern_t *p)
{
    int n = direction_tones(p);
    if (n < 2) return 1;
    return p->direction == BL_DIR_PENDULUM || p->direction == BL_DIR_M_PENDULUM ? 2 * (n - 1) : n;
}

int bl_direction_valid(const bl_pattern_t *p)
{
    if (p->direction >= BL_NDIRS) return 0;
    int period = direction_period(p);
    /* M-PENDULUM's zero is a one-time root pickup. Thereafter 1..period
     * encodes its regular traversal, keeping the pickup out of later loops. */
    return p->direction == BL_DIR_M_PENDULUM ? p->direction_cursor <= period : p->direction_cursor < period;
}

bl_pitch_t bl_direction_pitch(const bl_pattern_t *p, int onset, bl_tick_t completed_loops)
{
    if (onset < 0 || onset >= p->n) return (bl_pitch_t){0};
    if ((p->generator != BL_GEN_ARP && p->generator != BL_GEN_MOTIF) ||
        p->direction == BL_DIR_UP || !bl_direction_valid(p)) return p->note[onset].pitch;
    int n = direction_tones(p), period = direction_period(p);
    /* Reduce before multiplication: bounded 32-bit arithmetic on the FM-1.
     * Live rendering passes zero (its cursor is advanced at boundaries); direct
     * harmony rendering supplies the absolute loop ordinal for window independence. */
    int step = p->direction_cursor + onset + (int)bl_floor_mod(completed_loops, period) * p->n, index;
    if (p->direction == BL_DIR_M_PENDULUM && !step && completed_loops) step = period;
    if (n < 2) return p->note[0].pitch;
    switch (p->direction) {
    case BL_DIR_DOWN: index = n - 1 - step % n; break;
    case BL_DIR_M_DOWN: index = (n - step % n) % n; break;
    case BL_DIR_PENDULUM:
    case BL_DIR_M_PENDULUM: {
        int period = 2 * (n - 1);
        int phase = p->direction == BL_DIR_M_PENDULUM && step ? (step + n - 2) % period : step % period;
        index = phase < n ? phase : period - phase;
        break;
    }
    default: return p->note[onset].pitch;
    }
    return p->note[index].pitch;
}

void bl_direction_advance(bl_pattern_t *p)
{
    if ((p->generator != BL_GEN_ARP && p->generator != BL_GEN_MOTIF) || !p->n) return;
    if (!bl_direction_valid(p)) {
        p->direction = BL_DIR_UP;
        p->direction_cursor = 0;
    }
    int period = direction_period(p), next = p->direction_cursor + p->n;
    p->direction_cursor = (uint8_t)(p->direction == BL_DIR_M_PENDULUM ? (next - 1) % period + 1 : next % period);
}

/* a * b of ticks, out of line: a 64-bit multiply beside a load may corrupt a register on the FM-1 (felucca-bsp
 * docs/CPU.md) */
__attribute__((noinline)) static bl_tick_t tick_mul(bl_tick_t a, bl_tick_t b) { return a * b; }

/* ------------------------------------------------------------ loop cursor --- */
/* Pattern.occurrences(in:): a note at loop position p sounds at g = p - startOffset + k * length. Each onset is
 * inserted after every one it does not sort before (as Swift's stable sort of the notes' onsets in note order); one
 * that falls past the buffer's end is counted, not written, so the buffer holds the first max of the sorted result */
int bl_occurrences(const bl_pattern_t *p, bl_tick_t lo, bl_tick_t hi, bl_occ_t *out, int max)
{
    int total = 0, n = 0;
    if (p->n == 0 || lo >= hi) return 0;
    for (int i = 0; i < p->n; i++) {
        bl_tick_t base = p->note[i].position - p->start_offset, pos = p->note[i].position;
        /* the least k with base + k * length >= lo */
        bl_tick_t g = base + tick_mul(bl_floor_div(lo - base + p->length - 1, p->length), p->length);
        for (; g < hi; g += p->length) {
            if (g < lo) continue;
            total++;
            /* result.sort { at, then the note's position } */
            int j = n;
            while (j > 0 && (g < out[j - 1].at || (g == out[j - 1].at && pos < p->note[out[j - 1].index].position)))
                j--;
            if (j >= max) continue;
            for (int k = n < max ? n : max - 1; k > j; k--) out[k] = out[k - 1];
            out[j] = (bl_occ_t){g, (uint8_t)i};
            if (n < max) n++;
        }
    }
    return total;
}

/* Pattern.hasOnset(atGlobalTick:) */
int bl_has_onset(const bl_pattern_t *p, bl_tick_t global)
{
    bl_tick_t local = bl_floor_mod(global + p->start_offset, p->length);
    for (int i = 0; i < p->n; i++)
        if (p->note[i].position == local) return 1;
    return 0;
}

/* Pattern.firstSoundingNote: notes.min { position }, the first of equals */
int bl_first_sounding(const bl_pattern_t *p)
{
    int best = -1;
    for (int i = 0; i < p->n; i++)
        if (best < 0 || p->note[i].position < p->note[best].position) best = i;
    return best;
}

/* Pattern.withLength(_:); Swift's precondition newLength > 0 is the -1 */
int bl_with_length(bl_pattern_t *p, bl_tick_t new_len)
{
    int n = 0;
    if (new_len <= 0) return -1;
    p->length = new_len;
    for (int i = 0; i < p->n; i++)
        if (p->note[i].position < new_len) p->note[n++] = p->note[i];
    p->n = (uint8_t)n;
    if (!bl_direction_valid(p)) p->direction_cursor = 0;
    p->start_offset = bl_floor_mod(p->start_offset, new_len);
    return 0;
}

/* ------------------------------------------------------------ pattern ops --- */
/* densityCap's rank: (accent ? 0 : 1, on a beat ? 0 : 1, position), compared as Swift compares tuples */
static int rank_less(const bl_pnote_t *a, const bl_pnote_t *b)
{
    const bl_meter_t m = {4, 4};
    int a0 = a->accent ? 0 : 1, b0 = b->accent ? 0 : 1;
    int a1 = a->position % bl_ticks_per_beat(m) == 0 ? 0 : 1, b1 = b->position % bl_ticks_per_beat(m) == 0 ? 0 : 1;
    return a0 != b0 ? a0 < b0 : a1 != b1 ? a1 < b1 : a->position < b->position;
}

/* PatternOps.densityCap: keep ceil(count * density) notes, the best ranked (a stable sort), in their loop order */
void bl_density_cap(const bl_pattern_t *in, f64 density, bl_pattern_t *out)
{
    f64 clamped = f64_max(F64(0x0000000000000000), f64_min(F64(0x3FF0000000000000), density));   /* 0, 1 */
    if (out != in) *out = *in;
    if (!f64_lt(clamped, F64(0x3FF0000000000000)) || out->n == 0) return;
    int64_t keep = f64_to_i64(f64_ceil(f64_mul(f64_from_i64(out->n), clamped)));
    if (keep <= 0) {
        out->n = 0;
        return;
    }
    uint8_t order[BL_MAX_NOTES], kept[BL_MAX_NOTES];
    for (int i = 0; i < out->n; i++) {
        int j = i;
        for (; j > 0 && rank_less(&out->note[i], &out->note[order[j - 1]]); j--) order[j] = order[j - 1];
        order[j] = (uint8_t)i;
        kept[i] = 0;
    }
    for (int i = 0; i < keep && i < out->n; i++) kept[order[i]] = 1;
    int n = 0;
    for (int i = 0; i < out->n; i++)
        if (kept[i]) out->note[n++] = out->note[i];
    out->n = (uint8_t)n;
}

/* PatternOps.thinToRoots: the root-degree notes, or the first note retuned to the root when there is none; locked
 * and established */
void bl_thin_to_roots(bl_pattern_t *p)
{
    int n = 0;
    for (int i = 0; i < p->n; i++)
        if (p->note[i].pitch.degree == 0) p->note[n++] = p->note[i];
    if (n == 0 && p->n > 0) {
        p->note[0].pitch.degree = 0;
        n = 1;
    }
    p->n = (uint8_t)n;
    p->locked = 1;
    p->remaining = -1;
}

/* PatternOps.moveAccents: the first accent moves to a random unaccented note; with no accent, or none unaccented,
 * a random note's accent toggles */
void bl_move_accents(bl_pattern_t *p, uint64_t seed)
{
    bl_rng_t r;
    bl_rng_seed(&r, seed);
    bl_move_accents_with(p, &r);
}

void bl_move_accents_with(bl_pattern_t *p, bl_rng_t *r)
{
    int from = -1, plain = 0;
    for (int i = 0; i < p->n; i++) {
        if (p->note[i].accent && from < 0) from = i;
        if (!p->note[i].accent) plain++;
    }
    /* the unaccented indices' randomElement is drawn only when an accent was found (if let ..., let ...) */
    if (from >= 0 && plain > 0) {
        int64_t k = bl_rand_index(r, plain);
        for (int i = 0; i < p->n; i++)
            if (!p->note[i].accent && k-- == 0) {
                p->note[from].accent = 0;
                p->note[i].accent = 1;
                return;
            }
    }
    if (p->n > 0) {
        int64_t i = bl_rand_index(r, p->n);
        p->note[i].accent = !p->note[i].accent;
    }
}
