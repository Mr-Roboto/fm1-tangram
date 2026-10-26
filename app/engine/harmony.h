/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Global diatonic transposition as Berlinan has it (Harmony.swift): the schedule, a timeline of degree offsets by
 * bar, where a request binds to the next bar line; the note pipeline of one voice (degrees, transposition per onset,
 * key, register fold); and the default velocity policy (VelocityPolicy.swift). An empty schedule is the pedal on the
 * root. Not ported, as no template reaches them: other quanta than one bar, a velocity policy other than the
 * default. */
#pragma once
#include <stdint.h>
#include "bl.h"
#include "music.h"
#include "legit.h"

/* TranspositionEvent */
typedef struct {
    int32_t bar, offset;        /* bar >= 0; degreeOffset */
} bl_tev_t;

/* TranspositionSchedule: sorted by bar, one event per bar */
typedef struct bl_sched {
    uint16_t n;
    bl_tev_t ev[BL_MAX_EVENTS];
} bl_sched_t;

/* TranspositionSchedule(events: old + add): the add events in their order, the last one of a bar winning, into the
 * schedule. Swift's schedule has no bound; when this one would pass BL_MAX_EVENTS it first drops the events from bar
 * 16 on that another event at or before now_bar follows (no answer for bars below 16, which the bass melody's walk
 * reads, or at or after now_bar changes). If that is not room enough, nothing is added and -1 returned (the UI's
 * "SCHEDULE FULL"); else 0 */
int bl_sched_merge(bl_sched_t *s, const bl_tev_t *add, int n, int32_t now_bar);
/* TranspositionSchedule.offset(atBar:): the latest event at or before the bar, 0 before the first */
int32_t bl_sched_offset(const bl_sched_t *s, int32_t bar);

/* HarmonyEngine.quantizedEventBar(requestTick:meter: 4/4, quantumBars: 1): the next bar line; a request on one binds
 * there. request >= 0 and below 2^31 bars */
int32_t bl_quantized_bar(bl_tick_t request);

/* RenderedNote: voice is the voice's index in the project (Swift's voiceID) */
typedef struct {
    bl_tick_t at, gate;
    uint8_t voice, midi, velocity, accent, ratchet;
} bl_note_t;

/* HarmonyEngine.render(voices: [v], schedule:key:meter: 4/4, in: lo..<hi): the voice's onsets in the window in
 * PhasingEngine.events' order, each transposed by the offset of its own bar (if the voice participates), resolved in
 * the key at the voice's reference C and folded into its register; the velocity is the pattern's. Returns the number
 * of notes and writes the first max of them (as snprintf: a return above max means the rest are missing) */
int bl_render_voice(const bl_voice_t *v, uint8_t voice_index, const bl_sched_t *s, const bl_key_t *key, bl_tick_t lo,
                    bl_tick_t hi, bl_note_t *out, int max);

/* VelocityPolicy().apply(velocity:accent:patternSeed:localPosition:): accent adds 20, then the humanization of
 * -10...10 drawn from the pattern's seed and the note's loop position, clamped to 1...127 */
uint8_t bl_velocity(uint8_t vel, int accent, uint64_t pattern_seed, bl_tick_t local);
