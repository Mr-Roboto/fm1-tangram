/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The live engine (live.h): LiveEngine.swift function by function, and OfflineRenderer's materialize and
 * makePattern. A voice is its index in the project wherever the engine keeps one (the arranger's directives, the
 * notes, the CC); an action names it by its id code (BL_VID_), as Swift names it by its id. The queues keep Swift's
 * order (stable insertion by bind tick) and refuse what does not fit. Every 64-bit multiply is in a noinline helper
 * (felucca-bsp docs/CPU.md). */
#include <string.h>
#include "live.h"
#include "gen.h"
#include "pattern.h"
#include "rng.h"

#define NONE (-1)
#define ZERO F64(0x0000000000000000)    /* 0.0 */
#define HALF F64(0x3FE0000000000000)    /* 0.5 */
#define ONE F64(0x3FF0000000000000)     /* 1.0 */

static const bl_arrcfg_t ARR_DEFAULT = {0, 0, 0, 8, 16};      /* ArrangementConfig() */
static const int8_t BPART_STEPS[3] = {5, 3, 0};               /* BPartConfig.default: VI, iv, home */
enum { BPART_BARS = 4 };                                      /* its barsPerStep (swapBass, no dropBed) */

/* ------------------------------------------------------------- helpers --- */
/* bars * ticksPerBar and the bar of a tick >= 0, out of line (CPU.md: no 64-bit multiply inline, nor a division by
 * a constant, which the compiler may make one) */
static __attribute__((noinline)) bl_tick_t bar_tick(int64_t bar) { return bar * BL_TICKS_PER_BAR; }
static __attribute__((noinline)) int64_t bar_of(bl_tick_t t) { return t / BL_TICKS_PER_BAR; }

static bl_tick_t max_tick(bl_tick_t a, bl_tick_t b) { return a > b ? a : b; }
static bl_tick_t min_tick(bl_tick_t a, bl_tick_t b) { return a < b ? a : b; }

/* max(0, min(1, value)) */
static f64 clamp01(f64 v) { return f64_max(ZERO, f64_min(ONE, v)); }

/* the bar of the render head: the first bar a later render (or a regenerated walk past bar 16) reads, so
 * bl_sched_merge may compact what lies before it */
static int32_t head_bar(const bl_live_t *e) { return (int32_t)bar_of(e->rendered); }

/* withState(_:): the state of the voice with that id code, NULL when the engine has none */
static bl_vstate_t *state_of(bl_live_t *e, uint8_t vid)
{
    for (int i = 0; i < e->project.nvoices; i++)
        if (e->st[i].vid == vid && vid != BL_VID_NONE) return &e->st[i];
    return NULL;
}

/* ------------------------------------------------------------ patterns --- */
/* OfflineRenderer.makePattern(_:project:seedLabels:idSuffix:schedule:): the generator's pattern in the project's key
 * (the bass melody walks over sched), phased by phaseSteps 16ths, starting a full legitimization cycle of repeats */
static void make_pattern(const bl_project_t *p, int32_t repeats, const bl_vcfg_t *c, uint64_t seed,
                         const bl_sched_t *sched, bl_pattern_t *out)
{
    if (c->gen.kind == BL_GEN_BASS_MELODY)
        bl_make_bass_melody(c->gen.a, sched, seed, out);
    else
        bl_make_pattern(&c->gen, c->role, &p->key, seed, out);
    if (c->phase_steps) bl_with_length(out, out->length + (bl_tick_t)(c->phase_steps * (BL_PPQN / 4)));
    out->remaining = (int16_t)repeats;
}

/* OfflineRenderer.materialize(_:project:): the voice from its config, its pattern made with the labels "voice", id,
 * "gen0" over the project's own schedule; every voice transposes (no bass reharm, participatesInTransposition nil) */
static void materialize(const bl_project_t *p, int32_t repeats, const bl_vcfg_t *c, bl_voice_t *v)
{
    memset(v, 0, sizeof *v);
    v->id = c->id;
    v->role = c->role;
    v->reg_low = c->reg_low;
    v->reg_high = c->reg_high;
    v->ref_c = c->ref_c;
    v->participates = 1;
    v->anchor = c->anchor;
    uint64_t seed = bl_seed_derive(p->seed, "voice", c->id, "gen0", (const char *)NULL);
    make_pattern(p, repeats, c, seed, &p->transposition, &v->pat);
}

/* the legitimization's repeats (Legitimizer.Config.default: 4), scaled at the project level by an ambient arranger
 * that is on (min(32, repeats * 4)), so the initial lifecycles and every mutation see it */
static int32_t project_repeats(const bl_project_t *p)
{
    int32_t repeats = 4;
    if (p->has_arr && p->arr.enabled && p->arr.ambient) repeats = repeats * 4 < 32 ? repeats * 4 : 32;
    return repeats;
}

/* ambientPeriodBars(project:): max(8, Int((bpm / Double(beatsPerBar)).rounded())) */
static int32_t ambient_period(const bl_project_t *p)
{
    f64 per_minute = f64_div(f64_from_i64(p->bpm), F64(0x4010000000000000));    /* 4.0 */
    int64_t bars = f64_to_i64(f64_round(per_minute));
    return bars > 8 ? (int32_t)bars : 8;
}

static void arr_roles(const bl_project_t *p, uint8_t *roles)
{
    for (int i = 0; i < p->nvoices; i++) roles[i] = p->voice[i].role;
}

/* LiveEngine.init(project:): the legitimization's repeats (the ambient arranger's), the voices materialized, the
 * arranger made when the project's is on (its seed the labels "arrangement"), the voices it has not activated held
 * back */
void bl_live_init(bl_live_t *e, const bl_project_t *p)
{
    memset(e, 0, sizeof *e);
    e->project = *p;
    e->repeats = project_repeats(p);
    e->density = clamp01(p->density);
    e->tension = 2;                                     /* max(0, tensionBudget ?? 2) */
    e->sched = p->transposition;                        /* HarmonyScheduler.resolve: the legacy schedule */
    e->stop = NONE;
    for (int i = 0; i < p->nvoices; i++) {
        bl_vstate_t *s = &e->st[i];
        materialize(p, e->repeats, &p->voice[i], &s->voice);
        s->generation = 1;
        s->vid = (uint8_t)bl_vid(p->voice[i].id);
        s->active = 1;
        s->blank_req = s->regen_req = s->nudge_req = s->accent_req = NONE;
        int r = 0;                                      /* the voice's place among the ids, by string */
        for (int j = 0; j < p->nvoices; j++) r += strcmp(p->voice[j].id, p->voice[i].id) < 0;
        e->rank[i] = (uint8_t)r;
    }
    if (p->has_arr && p->arr.enabled) {
        uint8_t roles[BL_MAX_VOICES];
        arr_roles(p, roles);
        bl_arr_init(&e->arr, &p->arr, roles, p->nvoices, bl_seed_derive(p->seed, "arrangement", (const char *)NULL),
                    0, 0, ambient_period(p));
        e->has_arr = 1;
        for (int i = 0; i < p->nvoices; i++) {
            int on = 0;
            for (int k = 0; k < e->arr.nactive; k++) on |= e->arr.active[k] == i;
            if (!on) e->st[i].active = 0;
        }
    }
}

/* ------------------------------------------------------------- actions --- */
/* barBindTick(_:): the next bar line of the request, never one already rendered */
static bl_tick_t bar_bind(const bl_live_t *e, bl_tick_t request)
{
    return bar_tick(bl_quantized_bar(max_tick(request, e->rendered)));
}

/* enqueueGlobalChange(_:requestTick:): after every change bound at or before its tick */
static int enqueue_change(bl_live_t *e, bl_change_t c, bl_tick_t request)
{
    if (e->nchange >= BL_MAX_PENDING) return -1;
    c.tick = bar_bind(e, request);
    int k = 0;
    while (k < e->nchange && e->change[k].tick <= c.tick) k++;
    memmove(&e->change[k + 1], &e->change[k], (size_t)(e->nchange - k) * sizeof c);
    e->change[k] = c;
    e->nchange++;
    return 0;
}

/* the arranger's on/off and advance queues: the same insertion (firstIndex { $0.tick > tick }) */
static int enqueue_enable(bl_live_t *e, uint8_t on, bl_tick_t request)
{
    if (e->nen >= BL_MAX_ENABLES) return -1;
    bl_tick_t tick = bar_bind(e, request);
    int k = 0;
    while (k < e->nen && e->en[k].tick <= tick) k++;
    memmove(&e->en[k + 1], &e->en[k], (size_t)(e->nen - k) * sizeof e->en[0]);
    e->en[k].tick = tick;
    e->en[k].on = on;
    e->nen++;
    return 0;
}

static int enqueue_advance(bl_live_t *e, bl_tick_t request)
{
    if (e->nadv >= BL_MAX_ADVANCES) return -1;
    bl_tick_t tick = bar_bind(e, request);
    int k = 0;
    while (k < e->nadv && e->adv[k] <= tick) k++;
    memmove(&e->adv[k + 1], &e->adv[k], (size_t)(e->nadv - k) * sizeof e->adv[0]);
    e->adv[k] = tick;
    e->nadv++;
    return 0;
}

/* enqueue(_:atTick:). The voice requests and the lock act on the engine's voice of that id, if it has one; the
 * transposition merges at once (its bar the next one of the head); the rest bind at the next bar */
int bl_live_enqueue(bl_live_t *e, const bl_action_t *a, bl_tick_t request)
{
    bl_vstate_t *s;
    bl_change_t c;
    memset(&c, 0, sizeof c);
    c.kind = a->kind;
    switch (a->kind) {
    case BL_A_TRANSPOSE: {
        bl_tev_t ev = {bl_quantized_bar(max_tick(request, e->rendered)), a->arg};
        return bl_sched_merge(&e->sched, &ev, 1, head_bar(e));
    }
    case BL_A_REGENERATE:
        if ((s = state_of(e, a->voice))) s->regen_req = request;
        return 0;
    case BL_A_BLANK:
        if ((s = state_of(e, a->voice))) s->blank_req = request;
        return 0;
    case BL_A_LOCK:
        if ((s = state_of(e, a->voice))) s->voice.pat.locked = a->arg != 0;
        return 0;
    case BL_A_RECALL: {
        int k = 0;
        while (k < BL_MAX_RECALLS && e->recall_used[k]) k++;
        if (k == BL_MAX_RECALLS || e->nchange >= BL_MAX_PENDING) return -1;
        e->recall[k] = *a->state;
        e->recall_used[k] = 1;
        c.slot = (uint8_t)k;
        return enqueue_change(e, c, request);
    }
    case BL_A_KEY:
        c.key = a->key;
        return enqueue_change(e, c, request);
    case BL_A_DENSITY:
        c.value = a->value;
        return enqueue_change(e, c, request);
    case BL_A_BPART:
        return enqueue_change(e, c, request);
    case BL_A_VOICE_ACTIVE:
        c.voice = a->voice;
        c.on = a->arg != 0;
        return enqueue_change(e, c, request);
    case BL_A_TENSION:
        c.voice = a->voice;
        return enqueue_change(e, c, request);
    case BL_A_SCENE:
        c.scene = a->scene;
        c.bars = a->arg;
        return enqueue_change(e, c, request);
    case BL_A_ARRANGER:
        return enqueue_enable(e, a->arg != 0, request);
    case BL_A_ADVANCE:
        return enqueue_advance(e, request);
    default:
        return -1;
    }
}

/* captureState() */
void bl_live_capture(const bl_live_t *e, bl_state_t *out)
{
    out->n = e->project.nvoices;
    for (int i = 0; i < out->n; i++) {
        out->v[i] = e->st[i].voice;
        out->generation[i] = e->st[i].generation;
    }
    out->sched = e->sched;
}

/* LiveEngine(project:).captureState(), as bl_live_init leaves it: the materialized voices, generation 1, the
 * project's schedule (the arranger touches none of them) */
int bl_initial_state(const bl_project_t *p, bl_state_t *out)
{
    int32_t repeats = project_repeats(p);
    if (p->nvoices > BL_MAX_VOICES) return -1;
    out->n = p->nvoices;
    for (int i = 0; i < p->nvoices; i++) {
        materialize(p, repeats, &p->voice[i], &out->v[i]);
        out->generation[i] = 1;
    }
    out->sched = p->transposition;
    return 0;
}

/* applyRecall(_:at:): the schedule; for each voice the pattern (the whole voice) and generation of the state's voice
 * of its id, if there is one; every loop re-aligned on the bar, out of its blank and without its blank and
 * regenerate requests (the arranger's nudge and accent move stay) */
static void apply_recall(bl_live_t *e, const bl_state_t *snap, bl_tick_t tick)
{
    e->sched = snap->sched;
    for (int i = 0; i < e->project.nvoices; i++) {
        bl_vstate_t *s = &e->st[i];
        for (int j = 0; j < snap->n; j++)
            if (!strcmp(snap->v[j].id, s->voice.id)) {
                s->voice = snap->v[j];
                s->generation = snap->generation[j];
                break;
            }
        s->seg_start = tick;
        s->blank = 0;
        s->blank_req = s->regen_req = NONE;
    }
}

/* ---------------------------------------------------------- scene morphs --- */
/* "voiceID|target", the key sceneMorphs sorts by */
static void morph_key(const bl_live_t *e, const bl_smorph_t *m, char *buf, int size)
{
    const bl_vcfg_t *c = &e->project.voice[m->voice];
    int n = 0;
    for (const char *s = c->id; *s && n < size - 1; s++) buf[n++] = *s;
    if (n < size - 1) buf[n++] = '|';
    for (const char *s = c->cc_name[m->target]; *s && n < size - 1; s++) buf[n++] = *s;
    buf[n] = 0;
}

/* startSceneMorph(named:bars:at:) with the project's scene at that index (-1: no such scene): each destination's
 * morph departs from the key's morph's value at the tick (0.5 when it has none), toward the destination clamped to
 * 0-1, over max(1, bars) bars */
static void start_morph(bl_live_t *e, int scene, int32_t bars, bl_tick_t tick)
{
    if (scene < 0) return;
    const bl_scene_t *sc = &e->project.scene[scene];
    bl_tick_t duration = bar_tick(bars > 1 ? bars : 1);
    for (int j = 0; j < sc->n; j++) {
        int k = 0;
        while (k < e->nmorph && !(e->morph[k].voice == sc->voice[j] && e->morph[k].target == sc->target[j])) k++;
        f64 current = HALF;
        if (k < e->nmorph)
            current = bl_morph_value(&e->morph[k].m, tick);
        else if (k == BL_MAX_MORPHS)
            continue;                                   /* (a project's scenes name at most its voices' targets) */
        else
            e->nmorph++;
        bl_morph_t m = {current, clamp01(sc->value[j]), tick, tick + duration};
        e->morph[k] = (bl_smorph_t){sc->voice[j], sc->target[j], m};
    }
}

/* the project's scene of a BL_SCN_ name code, -1 for none (ccScenes?.first { $0.name == name }) */
static int scene_named(const bl_live_t *e, uint8_t code)
{
    if (code >= BL_NSCNS) return -1;
    for (int k = 0; k < e->project.nscenes; k++)
        if (!strcmp(e->project.scene[k].name, BL_SCN_NAME[code])) return k;
    return -1;
}

/* ------------------------------------------------------------- B part --- */
/* applyBPart(at:) with BPartConfig.default: the walk [5, 3, 0] from the fire bar, 4 bars a step, into the schedule;
 * every bass regenerated at its next loop boundary */
static void apply_bpart(bl_live_t *e, bl_tick_t tick)
{
    int32_t fire = (int32_t)bar_of(tick);
    bl_tev_t ev[3];
    for (int i = 0; i < 3; i++) ev[i] = (bl_tev_t){fire + i * BPART_BARS, BPART_STEPS[i]};
    bl_sched_merge(&e->sched, ev, 3, head_bar(e));
    for (int i = 0; i < e->project.nvoices; i++)
        if (e->st[i].voice.role == BL_ROLE_BASS) e->st[i].regen_req = tick;
}

/* ---------------------------------------------------- loop boundaries --- */
/* processBoundary(_:at:), the only place patterns change: (1) an audible play counts toward legitimization; (2) a
 * due blank freezes the rest; (2c) the arranger's accent move is the play's one change; (2d) its nudge fast-forwards
 * the lifecycle; (3) a regeneration beats the mutation; (4) the mutation, when the pattern may take one. Locked
 * patterns skip 2c, 2d and 3. (2b replace, 2c' and 2c'' melody and 5 accumulator: not ported) */
static void process_boundary(bl_live_t *e, int i, bl_tick_t tick)
{
    bl_vstate_t *s = &e->st[i];
    bl_pattern_t *p = &s->voice.pat;
    char gen[24];
    int was_blank = s->blank;
    s->blank = 0;
    if (!was_blank) bl_advance_boundary(p);
    if (s->blank_req >= 0 && tick >= s->blank_req) {
        s->blank_req = NONE;
        s->blank = 1;
        return;
    }
    if (s->accent_req >= 0 && tick >= s->accent_req) {
        s->accent_req = NONE;
        if (!p->locked) {
            bl_dec(s->generation, gen);
            bl_move_accents(p, bl_seed_derive(e->project.seed, "voice", s->voice.id, "accentMove", gen,
                                              (const char *)NULL));
            p->remaining = (int16_t)(e->repeats - 1);
            s->generation++;
            return;
        }
    }
    if (s->nudge_req >= 0 && tick >= s->nudge_req) {
        s->nudge_req = NONE;
        if (!p->locked) p->remaining = -1;
    }
    if (s->regen_req >= 0 && tick >= s->regen_req) {
        s->regen_req = NONE;
        if (!p->locked) {
            bl_dec(s->generation, gen);
            uint64_t seed = bl_seed_derive(e->project.seed, "voice", e->project.voice[i].id, "regen", gen,
                                           (const char *)NULL);
            make_pattern(&e->project, e->repeats, &e->project.voice[i], seed, &e->sched, p);
            s->generation++;
            return;
        }
    }
    if (bl_is_mutable(p)) {
        bl_dec(s->generation, gen);
        uint64_t seed = bl_seed_derive(e->project.seed, "voice", s->voice.id, "mut", gen, (const char *)NULL);
        bl_mutate(&s->voice, &e->project.key, seed, e->repeats, BL_RATCHET_DEFAULT);
        s->generation++;
    }
}

/* -------------------------------------------------------------- render --- */
/* renderVoice(_:range:): the voice's segments (one loop each) through the window: an audible one's notes (density
 * cap below 1, ties the identity: no slides; the note pipeline; the velocity policy at the note's loop position
 * with the pattern's seed), and at each segment end inside the window the loop boundary. Appends at out[n] */
static int render_voice(bl_live_t *e, int i, bl_tick_t lo, bl_tick_t hi, bl_note_t *out, int n, int max)
{
    bl_vstate_t *s = &e->st[i];
    while (s->seg_start < hi) {
        bl_tick_t length = s->voice.pat.length, seg_end = s->seg_start + length;
        if (!s->blank) {
            bl_tick_t lower = max_tick(s->seg_start, lo), upper = min_tick(seg_end, hi);
            if (lower < upper) {
                uint64_t seed = s->voice.pat.seed;
                bl_tick_t offset = s->voice.pat.start_offset;
                const bl_voice_t *src = &s->voice;
                if (f64_lt(e->density, ONE)) {
                    e->capped = s->voice;
                    bl_density_cap(&s->voice.pat, e->density, &e->capped.pat);
                    src = &e->capped;
                }
                int room = n < max ? max - n : 0;
                int k = bl_render_voice(src, (uint8_t)i, &e->sched, &e->project.key, lower, upper,
                                        room ? out + n : out, room);
                for (int j = 0; j < k && j < room; j++) {
                    bl_note_t *q = &out[n + j];
                    q->velocity = bl_velocity(q->velocity, q->accent, seed, bl_floor_mod(q->at + offset, length));
                }
                n += k;
            }
        }
        if (seg_end > hi) break;
        process_boundary(e, i, seg_end);
        s->seg_start = seg_end;
    }
    return n;
}

/* the notes' order: (at, voice id, MIDI note) */
static int note_less(const bl_live_t *e, const bl_note_t *a, const bl_note_t *b)
{
    if (a->at != b->at) return a->at < b->at;
    if (a->voice != b->voice) return e->rank[a->voice] < e->rank[b->voice];
    return a->midi < b->midi;
}

/* renderWindow(_:): the active voices, then the tension one-shots bound in the window (the sustained minor second
 * on the root: DegreePitch(degree: 0, accidental: 1), velocity 110, a bar long, accented; one of a voice the engine
 * lacks is dropped unheard), the head moved on, and the window's notes sorted (an insertion sort: stable) */
static int render_window(bl_live_t *e, bl_tick_t lo, bl_tick_t hi, bl_note_t *out, int n, int max)
{
    if (lo >= hi) return n;
    int first = n;
    for (int i = 0; i < e->project.nvoices; i++)
        if (e->st[i].active) n = render_voice(e, i, lo, hi, out, n, max);
    for (int k = e->ntension - 1; k >= 0; k--) {
        if (e->tension_note[k].tick < lo || e->tension_note[k].tick >= hi) continue;
        bl_tick_t tick = e->tension_note[k].tick;
        uint8_t vid = e->tension_note[k].voice;
        memmove(&e->tension_note[k], &e->tension_note[k + 1],
                (size_t)(e->ntension - k - 1) * sizeof e->tension_note[0]);
        e->ntension--;
        for (int i = 0; i < e->project.nvoices; i++) {
            if (bl_vid(e->project.voice[i].id) != vid || vid == BL_VID_NONE) continue;
            const bl_vcfg_t *c = &e->project.voice[i];
            bl_pitch_t pitch = {0, 0, 1};
            uint8_t midi = bl_fold(c->reg_low, c->reg_high, bl_resolve(&e->project.key, pitch, c->ref_c));
            if (n < max) out[n] = (bl_note_t){tick, BL_TICKS_PER_BAR, (uint8_t)i, midi, 110, 1, 1};
            n++;
            break;
        }
    }
    e->rendered = max_tick(e->rendered, hi);
    int end = n < max ? n : max;
    for (int i = first + 1; i < end; i++) {
        bl_note_t x = out[i];
        int j = i;
        for (; j > first && note_less(e, &x, &out[j - 1]); j--) out[j] = out[j - 1];
        out[j] = x;
    }
    return n;
}

/* the change at its bind tick (renderResolvingChanges' switch) */
static void apply_change(bl_live_t *e, const bl_change_t *c)
{
    bl_vstate_t *s;
    switch (c->kind) {
    case BL_A_RECALL:
        apply_recall(e, &e->recall[c->slot], c->tick);
        e->recall_used[c->slot] = 0;
        break;
    case BL_A_KEY: e->project.key = c->key; break;      /* everything downstream follows */
    case BL_A_DENSITY: e->density = clamp01(c->value); break;
    case BL_A_BPART: apply_bpart(e, c->tick); break;
    case BL_A_VOICE_ACTIVE:
        if ((s = state_of(e, c->voice))) {
            s->active = c->on;
            if (c->on) {
                s->seg_start = c->tick;
                s->blank = 0;
            }
        }
        break;
    case BL_A_TENSION:
        if (e->tension > 0) {
            e->tension--;
            if (e->ntension < BL_MAX_TENSION) {
                e->tension_note[e->ntension].tick = c->tick;
                e->tension_note[e->ntension].voice = c->voice;
                e->ntension++;
            }
        }
        break;
    case BL_A_SCENE: start_morph(e, scene_named(e, c->scene), c->bars, c->tick); break;
    }
}

/* renderResolvingChanges(_:): the window split at each pending change bound before its end, in queue order: the
 * part before it, the change, then the rest (Swift's recursion as a loop) */
static int render_resolving(bl_live_t *e, bl_tick_t lo, bl_tick_t hi, bl_note_t *out, int n, int max)
{
    while (e->nchange && e->change[0].tick < hi) {
        bl_change_t c = e->change[0];
        if (c.tick > lo) n = render_window(e, lo, c.tick, out, n, max);
        e->nchange--;
        memmove(&e->change[0], &e->change[1], (size_t)e->nchange * sizeof c);
        apply_change(e, &c);
        lo = max_tick(c.tick, lo);
    }
    return render_window(e, lo, hi, out, n, max);
}

/* applyDirectives(_:at:) */
static void apply_directives(bl_live_t *e, const bl_dir_t *d, int n, bl_tick_t tick)
{
    for (int k = 0; k < n && k < BL_ARR_MAX_DIRS; k++) {
        bl_vstate_t *s = &e->st[d[k].voice];
        switch (d[k].kind) {
        case BL_DIR_ACTIVATE:
            s->active = 1;
            s->seg_start = tick;                        /* the loop restarts on the bar */
            s->blank = 0;
            break;
        case BL_DIR_DEACTIVATE: s->active = 0; break;
        case BL_DIR_NUDGE: s->nudge_req = tick; break;
        case BL_DIR_ACCENT: s->accent_req = tick; break;
        case BL_DIR_BLANK: s->blank_req = tick; break;
        case BL_DIR_DENSITY: e->density = clamp01(d[k].value); break;
        case BL_DIR_BPART: apply_bpart(e, tick); break;
        case BL_DIR_THIN: bl_thin_to_roots(&s->voice.pat); break;   /* at once: roots sound from this tick */
        case BL_DIR_STOP: e->stop = tick; break;
        }
    }
}

/* processArrangementBoundary(at:), once per bar line (the cursor): the due on/offs (on makes the SM, adopting the
 * active voices, from the project's config or the default, forced on; off drops it, the voices keep their
 * activity), the due advances, the bar's clock (not on the bar the SM was made, nor at tick 0), all against the
 * locks as they stand; then a phase change fires its scene and captures (bed) or recalls (payoff, with reprise) the
 * theme */
static void arr_boundary(bl_live_t *e, bl_tick_t tick)
{
    bl_dir_t d[BL_ARR_MAX_DIRS];
    if (tick < e->arr_cursor) return;
    e->arr_cursor = tick + 1;
    int created = 0;
    while (e->nen && e->en[0].tick <= tick) {
        uint8_t on = e->en[0].on;
        e->nen--;
        memmove(&e->en[0], &e->en[1], (size_t)e->nen * sizeof e->en[0]);
        if (on && !e->has_arr) {
            const bl_project_t *p = &e->project;
            bl_arrcfg_t cfg = p->has_arr ? p->arr : ARR_DEFAULT;
            uint8_t roles[BL_MAX_VOICES];
            uint32_t mask = 0;
            cfg.enabled = 1;
            arr_roles(p, roles);
            for (int i = 0; i < p->nvoices; i++)
                if (e->st[i].active) mask |= 1u << i;
            bl_arr_init(&e->arr, &cfg, roles, p->nvoices, bl_seed_derive(p->seed, "arrangement", (const char *)NULL),
                        1, mask, ambient_period(p));
            e->has_arr = 1;
            created = 1;
        } else if (!on) {
            e->has_arr = 0;
        }
    }
    if (!e->has_arr) return;
    uint8_t before = e->arr.phase;
    uint32_t locked = 0;
    for (int i = 0; i < e->project.nvoices; i++)
        if (e->st[i].voice.pat.locked) locked |= 1u << i;
    while (e->nadv && e->adv[0] <= tick) {
        e->nadv--;
        memmove(&e->adv[0], &e->adv[1], (size_t)e->nadv * sizeof e->adv[0]);
        apply_directives(e, d, bl_arr_advance(&e->arr, locked, d, BL_ARR_MAX_DIRS), tick);
    }
    if (tick > 0 && !created) apply_directives(e, d, bl_arr_tick_bar(&e->arr, locked, d, BL_ARR_MAX_DIRS), tick);
    uint8_t phase = e->arr.phase;
    if (phase == before) return;
    start_morph(e, e->project.scene_by_phase[phase], 8, tick);
    if (phase == BL_PH_BED && !e->has_theme) {
        bl_live_capture(e, &e->theme);
        e->has_theme = 1;
    }
    if (phase == BL_PH_PAYOFF && e->project.has_arr && e->project.arr.reprise && e->has_theme)
        apply_recall(e, &e->theme, tick);
}

/* render(range:): nothing at or after the stop; with the arranger on (or an on/off pending) the window splits at
 * every bar line, where the arranger's boundary runs first */
int bl_live_render(bl_live_t *e, bl_tick_t lo, bl_tick_t hi, bl_note_t *out, int max)
{
    int n = 0;
    if (e->stop >= 0) {
        if (lo >= e->stop) return 0;
        hi = min_tick(hi, e->stop);
    }
    if (!e->has_arr && !e->nen) return render_resolving(e, lo, hi, out, 0, max);
    for (bl_tick_t t = lo; t < hi;) {
        int64_t bar = bar_of(t);
        if (t == bar_tick(bar)) arr_boundary(e, t);
        if (e->stop >= 0 && t >= e->stop) break;
        bl_tick_t end = min_tick(hi, bar_tick(bar + 1));
        if (e->stop >= 0) end = min_tick(end, e->stop);
        n = render_resolving(e, t, end, out, n, max);
        t = end;
    }
    return n;
}

/* ------------------------------------------------------------------ CC --- */
/* renderCC(range:): per voice ModulationEngine.render (its lanes through its CC map, an unmapped lane skipped, then
 * sorted by (at, controller), stably), then sceneEmissions (the morphs in the order of their keys, each with its
 * voice's controller for the target) */
int bl_live_render_cc(const bl_live_t *e, bl_tick_t lo, bl_tick_t hi, bl_cc_t *out, int max)
{
    const bl_project_t *p = &e->project;
    int n = 0;
    for (int i = 0; lo < hi && i < p->nvoices; i++) {
        const bl_vcfg_t *c = &p->voice[i];
        int first = n;
        for (int l = 0; l < c->nlanes; l++) {
            if (c->lane[l].target >= c->ncc) continue;
            int room = n < max ? max - n : 0;
            n += bl_cc_lane(&c->lane[l], c->cc_num[c->lane[l].target], (uint8_t)i, lo, hi, room ? out + n : out, room);
        }
        int end = n < max ? n : max;
        for (int a = first + 1; a < end; a++) {
            bl_cc_t x = out[a];
            int b = a;
            for (; b > first; b--) {
                const bl_cc_t *y = &out[b - 1];
                if (!(x.at < y->at || (x.at == y->at && x.controller < y->controller))) break;
                out[b] = *y;
            }
            out[b] = x;
        }
    }
    uint8_t order[BL_MAX_MORPHS];
    char ka[48], kb[48];
    for (int k = 0; k < e->nmorph; k++) {
        int j = k;
        morph_key(e, &e->morph[k], ka, sizeof ka);
        for (; j > 0; j--) {
            morph_key(e, &e->morph[order[j - 1]], kb, sizeof kb);
            if (strcmp(ka, kb) >= 0) break;
            order[j] = order[j - 1];
        }
        order[j] = (uint8_t)k;
    }
    for (int k = 0; k < e->nmorph; k++) {
        const bl_smorph_t *m = &e->morph[order[k]];
        const bl_vcfg_t *c = &p->voice[m->voice];
        if (m->target >= c->ncc) continue;
        int room = n < max ? max - n : 0;
        n += bl_morph_emit(&m->m, m->voice, c->cc_num[m->target], lo, hi, room ? out + n : out, room);
    }
    return n;
}

bl_tick_t bl_live_stop_tick(const bl_live_t *e) { return e->stop; }

int32_t bl_live_offset(const bl_live_t *e, int32_t bar) { return bl_sched_offset(&e->sched, bar); }

/* ------------------------------------------------------------ snapshot --- */
const char *const BL_UP_RAW[BL_NUPS] = {"key", "recall", "density", "bPart", "voiceActive", "tension", "arranger",
                                        "advance", "scene"};

static const uint8_t UP_OF_CHANGE[] = {
    [BL_A_RECALL] = BL_UP_RECALL, [BL_A_KEY] = BL_UP_KEY, [BL_A_DENSITY] = BL_UP_DENSITY,
    [BL_A_BPART] = BL_UP_BPART, [BL_A_VOICE_ACTIVE] = BL_UP_VOICE_ACTIVE, [BL_A_TENSION] = BL_UP_TENSION,
    [BL_A_SCENE] = BL_UP_SCENE,
};

/* snapshot(): the voices (pendingRequest: blank before regenerate), upcomingEvents() (the changes, the on/offs, the
 * advances, then sorted by tick, stably), the arranger's status, the density, the tension left, the key */
void bl_live_status(const bl_live_t *e, bl_status_t *out)
{
    memset(out, 0, sizeof *out);
    out->nvoices = e->project.nvoices;
    for (int i = 0; i < out->nvoices; i++) {
        const bl_vstate_t *s = &e->st[i];
        out->voice[i].vid = s->vid;
        out->voice[i].active = s->active;
        out->voice[i].blank = s->blank;
        if (s->blank_req >= 0) {
            out->voice[i].pending = BL_REQ_BLANK;
            out->voice[i].pending_tick = s->blank_req;
        } else if (s->regen_req >= 0) {
            out->voice[i].pending = BL_REQ_REGENERATE;
            out->voice[i].pending_tick = s->regen_req;
        }
        out->voice[i].locked = s->voice.pat.locked;
        out->voice[i].length = s->voice.pat.length;
        out->voice[i].remaining = s->voice.pat.remaining;
        out->voice[i].generation = s->generation;
    }
    int n = 0;
    for (int k = 0; k < e->nchange; k++) {
        const bl_change_t *c = &e->change[k];
        bl_upcoming_t *u = &out->up[n++];
        u->tick = c->tick;
        u->kind = UP_OF_CHANGE[c->kind];
        u->voice = c->kind == BL_A_VOICE_ACTIVE || c->kind == BL_A_TENSION ? c->voice : BL_VID_NONE;
        u->on = c->on;
        u->scene = c->scene;
        u->root = c->key.root;
        u->value = c->value;
    }
    for (int k = 0; k < e->nen; k++)
        out->up[n++] = (bl_upcoming_t){e->en[k].tick, BL_UP_ARRANGER, BL_VID_NONE, e->en[k].on, 0, 0, ZERO};
    for (int k = 0; k < e->nadv; k++)
        out->up[n++] = (bl_upcoming_t){e->adv[k], BL_UP_ADVANCE, BL_VID_NONE, 0, 0, 0, ZERO};
    for (int a = 1; a < n; a++) {
        bl_upcoming_t x = out->up[a];
        int b = a;
        for (; b > 0 && x.tick < out->up[b - 1].tick; b--) out->up[b] = out->up[b - 1];
        out->up[b] = x;
    }
    out->nup = (uint8_t)n;
    out->has_arr = e->has_arr;
    if (e->has_arr) {
        out->phase = e->arr.phase;
        out->bars_in_phase = e->arr.bars_in_phase;
        out->bars_until_change = e->arr.bars_until_change;
        out->nactive = e->arr.nactive;
        memcpy(out->active, e->arr.active, e->arr.nactive);
    }
    out->density = e->density;
    out->tension = e->tension;
    out->key = e->project.key;
    out->bar = head_bar(e);
    out->scene = BL_SCN_NONE;
    for (int s = 0; s < e->project.nscenes && out->scene == BL_SCN_NONE; s++) {
        const bl_scene_t *sc = &e->project.scene[s];
        int j = 0;
        for (; j < sc->n; j++) {        /* a morph of the target to the scene's value (its end: the value clamped) */
            int k = 0;
            while (k < e->nmorph && !(e->morph[k].voice == sc->voice[j] && e->morph[k].target == sc->target[j]
                                      && f64_eq(e->morph[k].m.end, clamp01(sc->value[j]))))
                k++;
            if (k == e->nmorph) break;
        }
        if (sc->n && j == sc->n) out->scene = (uint8_t)bl_scn(sc->name);
    }
}

/* ---------------------------------------------------------- state text --- */
/* a line of the text, built without printf */
typedef struct {
    char s[256];
    int n;
} line_t;

static void put(line_t *l, const char *s)
{
    while (*s && l->n < (int)sizeof l->s - 1) l->s[l->n++] = *s++;
    l->s[l->n] = 0;
}

static void put_i(line_t *l, int64_t v)
{
    char d[24];
    bl_dec(v, d);
    put(l, " ");
    put(l, d);
}

/* v / 10, its remainder in *digit, out of line */
static __attribute__((noinline)) uint64_t div10(uint64_t v, char *digit)
{
    *digit = (char)('0' + v % 10);
    return v / 10;
}

static void put_u(line_t *l, uint64_t v)
{
    char d[24];
    int k = 23;
    d[k] = 0;
    do v = div10(v, &d[--k]); while (v != 0);
    put(l, " ");
    put(l, d + k);
}

static void put_w(line_t *l, const char *w)
{
    put(l, " ");
    put(l, w);
}

static void put_hex(line_t *l, uint64_t v)
{
    char d[17];
    for (int k = 15; k >= 0; k--, v >>= 4) d[k] = "0123456789abcdef"[v & 15];
    d[16] = 0;
    put_w(l, d);
}

/* Swift's description of (value * 100).rounded() / 100: the shortest digits that read back as it, at least one
 * after the point ("0.35", "1.0", "-0.0"); value within +-2 * 10^7 (a density is 0-1) */
static void put_density(line_t *l, f64 value)
{
    f64 r = f64_round(f64_mul(value, F64(0x4059000000000000)));                 /* 100.0 */
    f64 d = f64_div(r, F64(0x4059000000000000));
    int32_t c = (int32_t)f64_to_i64(r);
    char buf[32];
    int k = 0;
    if (d.bits >> 63) buf[k++] = '-';
    if (c < 0) c = -c;
    char w[24];
    bl_dec(c / 100, w);
    for (char *s = w; *s; s++) buf[k++] = *s;
    buf[k++] = '.';
    buf[k++] = (char)('0' + c % 100 / 10);
    if (c % 10) buf[k++] = (char)('0' + c % 10);
    buf[k] = 0;
    put_w(l, buf);
}

static void emit(line_t *l, void (*line)(void *, const char *), void *ctx)
{
    line(ctx, l->s);
    l->n = 0;
    l->s[0] = 0;
}

/* tests/equiv/README.md "The state hash" (RefTool's Dump.stateText) */
void bl_live_text(const bl_live_t *e, void (*line)(void *ctx, const char *text), void *ctx)
{
    static bl_status_t st;          /* (2 KB: off the stack) */
    line_t l = {{0}, 0};
    bl_live_status(e, &st);
    for (int i = 0; i < st.nvoices; i++) {
        const bl_pattern_t *p = &e->st[i].voice.pat;
        put(&l, "voice");
        put_w(&l, e->st[i].voice.id);
        put_i(&l, st.voice[i].active);
        put_i(&l, st.voice[i].blank);
        if (st.voice[i].pending == BL_REQ_NONE) {
            put_w(&l, "-");
        } else {
            put_w(&l, st.voice[i].pending == BL_REQ_BLANK ? "blank" : "regenerate");
            put_i(&l, st.voice[i].pending_tick);
        }
        put_i(&l, st.voice[i].generation);
        put_i(&l, p->length);
        put_i(&l, p->start_offset);
        put_i(&l, p->locked);
        if (p->remaining < 0) put_w(&l, "E");
        else put_i(&l, p->remaining);
        put_u(&l, p->seed);
        emit(&l, line, ctx);
        for (int k = 0; k < p->n; k++) {
            const bl_pnote_t *q = &p->note[k];
            put(&l, "note");
            put_i(&l, q->position);
            put_i(&l, q->pitch.degree);
            put_i(&l, q->pitch.octave);
            put_i(&l, q->pitch.accidental);
            put_i(&l, q->velocity);
            put_i(&l, q->gate);
            put_i(&l, q->accent);
            put_i(&l, q->ratchet);
            emit(&l, line, ctx);
        }
    }
    for (int k = 0; k < e->sched.n; k++) {
        put(&l, "sched");
        put_i(&l, e->sched.ev[k].bar);
        put_i(&l, e->sched.ev[k].offset);
        emit(&l, line, ctx);
    }
    put(&l, "key");
    put_i(&l, st.key.root);
    for (int k = 0; k < st.key.scale.count; k++) put_i(&l, st.key.scale.off[k]);
    emit(&l, line, ctx);
    put(&l, "density");
    put_hex(&l, st.density.bits);
    emit(&l, line, ctx);
    put(&l, "tension");
    put_i(&l, st.tension);
    emit(&l, line, ctx);
    put(&l, "arr");
    if (st.has_arr) {
        put_w(&l, BL_PHASE_RAW[st.phase]);
        put_i(&l, st.bars_in_phase);
        put_i(&l, st.bars_until_change);
        for (int k = 0; k < st.nactive; k++) put_w(&l, e->project.voice[st.active[k]].id);
    } else {
        put_w(&l, "-");
    }
    emit(&l, line, ctx);
    for (int k = 0; k < st.nup; k++) {
        const bl_upcoming_t *u = &st.up[k];
        put(&l, "up");
        put_i(&l, u->tick);
        put_w(&l, BL_UP_RAW[u->kind]);
        switch (u->kind) {
        case BL_UP_KEY: put_i(&l, u->root); break;
        case BL_UP_DENSITY: put_density(&l, u->value); break;
        case BL_UP_VOICE_ACTIVE:
        case BL_UP_ARRANGER: put_w(&l, u->on ? "on" : "off"); break;
        case BL_UP_TENSION: put_w(&l, u->voice < BL_NVIDS ? BL_VID_NAME[u->voice] : "-"); break;
        case BL_UP_SCENE: put_w(&l, u->scene < BL_NSCNS ? BL_SCN_NAME[u->scene] : "-"); break;
        default: put_w(&l, "-"); break;
        }
        put_w(&l, u->voice < BL_NVIDS ? BL_VID_NAME[u->voice] : "-");
        emit(&l, line, ctx);
    }
}

/* FNV-1a 64's step, out of line */
static __attribute__((noinline)) uint64_t fnv_step(uint64_t h, uint8_t byte)
{
    return (h ^ byte) * UINT64_C(0x100000001b3);
}

static void hash_line(void *ctx, const char *text)
{
    uint64_t *h = ctx;
    for (; *text; text++) *h = fnv_step(*h, (uint8_t)*text);
    *h = fnv_step(*h, '\n');
}

uint64_t bl_live_hash(const bl_live_t *e)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    bl_live_text(e, hash_line, &h);
    return h;
}
