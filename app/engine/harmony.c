/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The transposition schedule, the note pipeline of one voice and the velocity policy (harmony.h). The schedule is
 * kept as Swift's init leaves it (sorted by bar, one event a bar), so a merge is an insert or a replace per event;
 * its bound (Swift's has none) is the compaction of bl_sched_merge. */
#include <stddef.h>
#include "harmony.h"
#include "rng.h"

static const bl_meter_t M = {4, 4};                 /* Meter.fourFour */

/* ------------------------------------------------------------- schedule --- */
/* the index of the event at bar, or of the first one after it (s->n when none) */
static int find_bar(const bl_sched_t *s, int32_t bar)
{
    int i = 0;
    while (i < s->n && s->ev[i].bar < bar) i++;
    return i;
}

/* the events the merge adds as new bars: in neither the schedule nor earlier in add */
static int new_bars(const bl_sched_t *s, const bl_tev_t *add, int n)
{
    int count = 0;
    for (int i = 0; i < n; i++) {
        int k = find_bar(s, add[i].bar), seen = k < s->n && s->ev[k].bar == add[i].bar;
        for (int j = 0; j < i && !seen; j++) seen = add[j].bar == add[i].bar;
        count += !seen;
    }
    return count;
}

/* drop each event from bar 16 on whose follower is at or before now_bar: the follower answers from there on, the
 * last event at or before now_bar has no such follower and stays, and the bars below 16 read only events below 16 */
static void compact(bl_sched_t *s, int32_t now_bar)
{
    int n = 0;
    for (int i = 0; i < s->n; i++)
        if (!(s->ev[i].bar >= 16 && i + 1 < s->n && s->ev[i + 1].bar <= now_bar)) s->ev[n++] = s->ev[i];
    s->n = (uint16_t)n;
}

/* TranspositionSchedule(events: s.events + add): byBar[event.bar] = event in input order (the last of a bar wins),
 * then byBar.values sorted by bar. The schedule is that already, so each add event replaces its bar's event or is
 * inserted in bar order. fresh is counted against the schedule the loop inserts into (after the compaction, which
 * may drop a bar an add event would have replaced), so the loop inserts exactly fresh events and s->n + fresh <=
 * BL_MAX_EVENTS holds it in bounds; the loop's own check is a second fence, never reached */
int bl_sched_merge(bl_sched_t *s, const bl_tev_t *add, int n, int32_t now_bar)
{
    int fresh = new_bars(s, add, n);
    if (s->n + fresh > BL_MAX_EVENTS) {
        compact(s, now_bar);
        fresh = new_bars(s, add, n);
        if (s->n + fresh > BL_MAX_EVENTS) return -1;
    }
    for (int i = 0; i < n; i++) {
        int k = find_bar(s, add[i].bar);
        if (k == s->n || s->ev[k].bar != add[i].bar) {
            if (s->n >= BL_MAX_EVENTS) return -1;
            for (int j = s->n; j > k; j--) s->ev[j] = s->ev[j - 1];
            s->n++;
        }
        s->ev[k] = add[i];
    }
    return 0;
}

/* TranspositionSchedule.offset(atBar:) */
int32_t bl_sched_offset(const bl_sched_t *s, int32_t bar)
{
    int32_t result = 0;
    for (int i = 0; i < s->n; i++) {
        if (s->ev[i].bar > bar) break;
        result = s->ev[i].offset;
    }
    return result;
}

/* HarmonyEngine.quantizedEventBar(requestTick:meter:quantumBars: 1): floorDiv(requestTick + quantum - 1, quantum) */
int32_t bl_quantized_bar(bl_tick_t request)
{
    bl_tick_t quantum = bl_ticks_per_bar(M);
    return (int32_t)bl_floor_div(request + quantum - 1, quantum);
}

/* --------------------------------------------------------- note pipeline --- */
/* floorDiv(at, ticksPerBar) for offset(atBar:); past the int32 ends the answer is the one at the end (the last
 * event's above, 0 below), so the bar is clamped there */
static int32_t bar_of(bl_tick_t at)
{
    int64_t bar = bl_floor_div(at, bl_ticks_per_bar(M));
    return bar > INT32_MAX ? INT32_MAX : bar < INT32_MIN ? INT32_MIN : (int32_t)bar;
}

/* HarmonyEngine.render(voices: [v], ...): PhasingEngine.events of one voice is its occurrences' order (at, then the
 * note's position). They are taken a loop length at a time, which holds each note at most once (BL_MAX_NOTES
 * onsets), and appended in order; the offset binds per onset, at the onset's bar */
int bl_render_voice(const bl_voice_t *v, uint8_t voice_index, const bl_sched_t *s, const bl_key_t *key, bl_tick_t lo,
                    bl_tick_t hi, bl_note_t *out, int max)
{
    const bl_pattern_t *p = &v->pat;
    bl_occ_t occ[BL_MAX_NOTES];
    int total = 0;
    for (bl_tick_t a = lo; a < hi; a += p->length) {
        bl_tick_t b = hi - a > p->length ? a + p->length : hi;
        int n = bl_occurrences(p, a, b, occ, BL_MAX_NOTES);
        for (int i = 0; i < n; i++, total++) {
            if (total >= max) continue;
            const bl_pnote_t *note = &p->note[occ[i].index];
            bl_pitch_t pitch = note->pitch;
            if (v->participates) pitch.degree += bl_sched_offset(s, bar_of(occ[i].at));
            int32_t resolved = bl_resolve(key, pitch, v->ref_c);
            out[total] = (bl_note_t){occ[i].at, note->gate, voice_index, bl_fold(v->reg_low, v->reg_high, resolved),
                                     note->velocity, note->accent, note->ratchet};
        }
    }
    return total;
}

/* -------------------------------------------------------------- velocity --- */
/* VelocityPolicy(accentBoost: 20, humanizeRange: 10).apply: v + boost, + roll % (2 * range + 1) - range, clamped;
 * roll = SeedStream.derive(master: patternSeed, "humanize", String(localPosition)) */
uint8_t bl_velocity(uint8_t vel, int accent, uint64_t pattern_seed, bl_tick_t local)
{
    enum { BOOST = 20, RANGE = 10 };
    char pos[24];
    int32_t v = vel;
    if (accent) v += BOOST;
    bl_dec(local, pos);
    uint64_t roll = bl_seed_derive(pattern_seed, "humanize", pos, (const char *)NULL);
    v += (int32_t)(roll % (2 * RANGE + 1)) - RANGE;
    return (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v);
}
