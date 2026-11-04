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
static void configured_pattern(const bl_vconfig_t *cfg, int role, int repeats, uint64_t seed,
                                const bl_sched_t *sched, bl_pattern_t *out)
{
    if (cfg->gen.kind == BL_GEN_ARP)
        bl_make_arp(&cfg->gen, &cfg->key, seed, cfg->length, out);
    else if (cfg->gen.kind == BL_GEN_MOTIF)
        bl_make_motif(&cfg->gen, &cfg->key, seed, cfg->length, out);
    else {
        if (cfg->gen.kind == BL_GEN_BASS_MELODY)
            bl_make_bass_melody(cfg->gen.a, sched, seed, out);
        else
            bl_make_pattern(&cfg->gen, role, &cfg->key, seed, out);
        bl_with_length(out, cfg->length);
    }
    out->remaining = (int16_t)repeats;
    out->pitch_phase = cfg->phase;
}

/* OfflineRenderer.makePattern(_:project:seedLabels:idSuffix:schedule:): the generator's pattern in the project's key
 * (the bass melody walks over sched), phased by phaseSteps 16ths, starting a full legitimization cycle of repeats */
static void make_pattern(const bl_project_t *p, int32_t repeats, const bl_vcfg_t *c, uint64_t seed,
                         const bl_sched_t *sched, bl_pattern_t *out)
{
    if (c->gen.kind == BL_GEN_ARP || c->gen.kind == BL_GEN_MOTIF) {
        /* Generate on the adjusted duration so longer loops also fill their grid.
         * Other generators retain withLength's existing truncate/extend semantics. */
        bl_tick_t length = BL_TICKS_PER_BAR + c->phase_steps * (BL_PPQN / 4);
        if (c->gen.kind == BL_GEN_MOTIF)
            bl_make_motif(&c->gen, &p->key, seed, length, out);
        else
            bl_make_arp(&c->gen, &p->key, seed, length, out);
        out->remaining = (int16_t)repeats;
        return;
    }
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
    v->delay = bl_delay_config(c->delay);
    v->midi_channel = c->channel & 15u;
    uint64_t seed = bl_seed_derive(p->seed, "voice", c->id, "gen0", (const char *)NULL);
    make_pattern(p, repeats, c, seed, &p->transposition, &v->pat);
    v->config.key = p->key;
    v->config.gen = c->gen;
    v->config.length = (uint32_t)v->pat.length;
    v->config.reg_low = c->reg_low;
    v->config.reg_high = c->reg_high;
    if (c->gen.kind == BL_GEN_ARP || c->gen.kind == BL_GEN_MOTIF) {
        v->config.gen.a = c->gen.a == 240 ? 240 : 480;
        v->config.gen.b = c->gen.b > 0 && c->gen.b < v->config.gen.a ? c->gen.b : v->config.gen.a / 2;
    } else if (v->config.gen.a < 1) v->config.gen.a = 1;
    if (c->has_config && bl_config_valid(&c->config)) {
        v->config = c->config;
        v->reg_low = c->config.reg_low;
        v->reg_high = c->config.reg_high;
        configured_pattern(&v->config, c->role, repeats, seed, &p->transposition, &v->pat);
    }
    v->pending = v->config;
    if (v->pat.generator == BL_GEN_MOTIF) bl_motif_evolution_init(&v->motif, &v->config.key, seed, c->id);
}

int bl_live_get_config(const bl_live_t *e, int vid, bl_vconfig_t *active, bl_vconfig_t *target)
{
    for (int i = 0; vid >= 0 && vid < BL_NVIDS && i < e->project.nvoices; i++)
        if (e->st[i].vid == vid) {
            if (active) *active = e->st[i].voice.config;
            if (target) *target = e->st[i].voice.pending;
            return 0;
        }
    return -1;
}

static void composer_cancel_voice(bl_live_t *e, int i)
{
    if (!(e->composer.requests >> i & 1u)) return;
    e->st[i].accent_req = e->st[i].nudge_req = NONE;
    e->composer.requests &= ~(1u << i);
}

static void composer_manual_voice(bl_live_t *e, int i)
{
    composer_cancel_voice(e, i);
    e->composer.manual_voices |= 1u << i;
}

int bl_live_set_config(bl_live_t *e, int vid, const bl_vconfig_t *target)
{
    if (!target || vid < 0 || vid >= BL_NVIDS || !bl_config_valid(target)) return -1;
    bl_vstate_t *s = state_of(e, (uint8_t)vid);
    if (!s) return -1;
    if (bl_config_diff(&s->voice.pending, target) & ((1u << BL_CFG_ROOT) | (1u << BL_CFG_SCALE)))
        e->composer.manual_harmony = 1;
    s->voice.pending = *target;
    s->voice.config_dirty = bl_config_diff(&s->voice.config, target);
    composer_manual_voice(e, (int)(s - e->st));
    return 0;
}

int bl_live_set_config_field(bl_live_t *e, int vid, int field, int32_t value)
{
    bl_vconfig_t target;
    if (bl_live_get_config(e, vid, NULL, &target) || bl_config_edit(&target, field, value)) return -1;
    return bl_live_set_config(e, vid, &target);
}

/* the legitimization's repeats (Legitimizer.Config.default: 4), scaled at the project level by an ambient arranger
 * that is on (min(32, repeats * 4)), so the initial lifecycles and every mutation see it */
static int32_t project_repeats(const bl_project_t *p)
{
    int32_t repeats = 4;
    if (p->has_arr && p->arr.enabled && p->arr.ambient) repeats = repeats * 4 < 32 ? repeats * 4 : 32;
    return repeats;
}

/* the legitimization's ratchetProbability (nil: Legitimizer's 0.15), which the ambient arranger's scaling keeps */
static f64 project_ratchet(const bl_project_t *p) { return p->has_ratchet ? p->ratchet_p : BL_RATCHET_DEFAULT; }

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
    if ((p->has_arr && p->arr.enabled) || p->composer.enabled) {
        uint8_t roles[BL_MAX_VOICES];
        arr_roles(p, roles);
        const bl_arrcfg_t *form = p->has_arr ? &p->arr : &bl_arr_classic;
        bl_arr_init(&e->arr, form, roles, p->nvoices, bl_seed_derive(p->seed, "arrangement", (const char *)NULL),
                    0, 0, ambient_period(p));
        e->has_arr = 1;
        if (p->composer.enabled) {
            bl_comp_init(&e->composer, &p->composer, form, p->seed);
            bl_comp_select(&e->composer, p->composer_start);
            e->composer.legacy_enabled = p->has_arr && p->arr.enabled;
            e->composer.legacy_form = p->has_arr ? p->arr : ARR_DEFAULT;
            e->arr.phase = e->composer.phase;
            e->arr.bars_until_change = e->composer.bars_left;
            memcpy(e->arr.active, e->arr.order, e->arr.norder);
            e->arr.nactive = e->arr.norder;
        } else for (int i = 0; i < p->nvoices; i++) {
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
        int r = bl_sched_merge(&e->sched, &ev, 1, head_bar(e));
        if (!r) e->composer.manual_harmony = 1;
        return r;
    }
    case BL_A_REGENERATE:
        if ((s = state_of(e, a->voice))) {
            composer_manual_voice(e, (int)(s - e->st));
            s->regen_req = request;
        }
        return 0;
    case BL_A_BLANK:
        if ((s = state_of(e, a->voice))) {
            composer_manual_voice(e, (int)(s - e->st));
            s->blank_req = request;
        }
        return 0;
    case BL_A_LOCK:
        if ((s = state_of(e, a->voice))) {
            composer_manual_voice(e, (int)(s - e->st));
            s->voice.pat.locked = a->arg != 0;
        }
        return 0;
    case BL_A_RECALL: {
        int k = 0;
        while (k < BL_MAX_RECALLS && e->recall_used[k]) k++;
        if (k == BL_MAX_RECALLS || e->nchange >= BL_MAX_PENDING) return -1;
        e->recall[k] = *a->state;
        e->recall_used[k] = 1;
        c.slot = (uint8_t)k;
        int r = enqueue_change(e, c, request);
        if (r) e->recall_used[k] = 0;     /* a full shared change queue must not leak a recall slot */
        if (!r) {
            for (int i = 0; i < e->project.nvoices; i++) composer_manual_voice(e, i);
            e->composer.manual_harmony = 1;
        }
        return r;
    }
    case BL_A_KEY:
        /* The legacy global key control broadcasts into the same per-voice
         * pending frames; each voice crosses into the new key on its own loop. */
        for (int i = 0; i < e->project.nvoices; i++) {
            bl_vconfig_t target = e->st[i].voice.pending;
            target.key = a->key;
            if (!bl_config_valid(&target)) return -1;
        }
        e->project.key = a->key;
        e->composer.manual_harmony = 1;
        for (int i = 0; i < e->project.nvoices; i++) composer_manual_voice(e, i);
        for (int i = 0; i < e->project.nvoices; i++) {
            e->st[i].voice.pending.key = a->key;
            e->st[i].voice.config_dirty = bl_config_diff(&e->st[i].voice.config, &e->st[i].voice.pending);
        }
        return 0;
    case BL_A_DENSITY:
        c.value = a->value;
        return enqueue_change(e, c, request);
    case BL_A_BPART: {
        int r = enqueue_change(e, c, request);
        if (!r) e->composer.manual_harmony = 1;
        return r;
    }
    case BL_A_VOICE_ACTIVE:
        c.voice = a->voice;
        c.on = a->arg != 0;
        return enqueue_change(e, c, request);
    case BL_A_TENSION:
        c.voice = a->voice;
        return enqueue_change(e, c, request);
    case BL_A_SCENE: {
        c.scene = a->scene;
        c.bars = a->arg;
        int r = enqueue_change(e, c, request);
        if (!r) e->composer.manual_scene = 1;
        return r;
    }
    case BL_A_ARRANGER:
        return enqueue_enable(e, a->arg != 0, request);
    case BL_A_ADVANCE:
        return enqueue_advance(e, request);
    case BL_A_COMPOSER: {
        uint32_t bits = (uint32_t)a->arg;
        bl_compcfg_t cfg = {(uint8_t)bits, (uint8_t)(bits >> 8), (uint8_t)(bits >> 16), (uint8_t)(bits >> 24)};
        if (!bl_comp_valid(&cfg) || (cfg.enabled && e->stop >= 0)) return -1;
        c.bars = a->arg;
        c.on = cfg.enabled;
        return enqueue_change(e, c, request);
    }
    default:
        return -1;
    }
}

int bl_live_set_composer(bl_live_t *e, const bl_compcfg_t *cfg, bl_tick_t request)
{
    if (!cfg || !bl_comp_valid(cfg)) return -1;
    bl_action_t a = {0};
    a.kind = BL_A_COMPOSER;
    a.arg = (int32_t)(cfg->enabled | (uint32_t)cfg->bars << 8 | (uint32_t)cfg->evolution << 16 | (uint32_t)cfg->tension << 24);
    return bl_live_enqueue(e, &a, request);
}

int bl_live_select_composer_stage(bl_live_t *e, int section, bl_tick_t request)
{
    if (section < 0 || section >= 7 || !e->project.composer.enabled || e->stop >= 0) return -1;
    /* Latest target wins; intermediate encoder positions are never entered.
     * Returning to the current stage cancels without restarting it. */
    e->composer.pending_stage = section == e->composer.section ? 0 : (uint8_t)(section + 1);
    e->composer.pending_tick = bar_bind(e, request);
    return 0;
}

/* captureState() */
void bl_live_capture(const bl_live_t *e, bl_state_t *out)
{
    out->n = e->project.nvoices;
    for (int i = 0; i < out->n; i++) {
        out->v[i] = e->st[i].voice;
        /* A snapshot's offset is the audible source-loop phase at capture,
         * not an absolute transport-grid offset. Keep this separate from the
         * local completion clock (legacy activation can offset those clocks). */
        out->v[i].pat.start_offset = bl_floor_mod(e->rendered + e->st[i].voice.pat.start_offset,
                                                 e->st[i].voice.pat.length);
        out->generation[i] = e->st[i].generation;
        out->motif_phase[i] = (uint32_t)bl_floor_mod(e->rendered - e->st[i].seg_start, e->st[i].voice.pat.length);
        out->motif_flags[i] = (uint8_t)(e->st[i].active | e->st[i].blank << 1);
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
        out->motif_phase[i] = 0;
        out->motif_flags[i] = 1;
    }
    if (p->has_arr && p->arr.enabled && !p->composer.enabled) {
        bl_arr_t arr;
        uint8_t roles[BL_MAX_VOICES];
        arr_roles(p, roles);
        bl_arr_init(&arr, &p->arr, roles, p->nvoices, bl_seed_derive(p->seed, "arrangement", (const char *)NULL),
                    0, 0, ambient_period(p));
        for (int i = 0; i < p->nvoices; i++) {
            out->motif_flags[i] = 0;
            for (int k = 0; k < arr.nactive; k++) if (arr.active[k] == i) out->motif_flags[i] = 1;
        }
    }
    out->sched = p->transposition;
    return 0;
}

/* Recall restores musical frames, pending edits and the captured local phase
 * for every matched voice. Requests to blank/regenerate are cleared as before;
 * the arranger's nudge/accent requests stay. */
static void apply_recall_mask(bl_live_t *e, const bl_state_t *snap, bl_tick_t tick,
                              uint32_t voices, int schedule, int output)
{
    if (schedule) e->sched = snap->sched;
    for (int i = 0; i < e->project.nvoices; i++) {
        if (!(voices >> i & 1u)) continue;
        bl_vstate_t *s = &e->st[i];
        bl_delay_t delay = s->voice.delay;
        uint8_t channel = s->voice.midi_channel;
        uint32_t phase = 0;
        int motif_flags = -1;
        for (int j = 0; j < snap->n; j++)
            if (!strcmp(snap->v[j].id, s->voice.id)) {
                s->voice = snap->v[j];
                if (!output) {
                    s->voice.delay = delay;
                    s->voice.midi_channel = channel;
                }
                s->generation = snap->generation[j];
                phase = snap->motif_phase[j];
                motif_flags = snap->motif_flags[j];
                e->project.voice[i].config = s->voice.config;
                e->project.voice[i].has_config = 1;
                e->project.voice[i].delay = s->voice.delay;
                e->project.voice[i].channel = s->voice.midi_channel;
                break;
            }
        s->seg_start = tick - phase;
        if (motif_flags >= 0)
            s->voice.pat.start_offset = bl_floor_mod(s->voice.pat.start_offset - tick, s->voice.pat.length);
        s->blank = 0;
        if (motif_flags >= 0) {
            s->active = (uint8_t)(motif_flags & 1);
            s->blank = (uint8_t)(motif_flags >> 1 & 1);
        }
        s->blank_req = s->regen_req = NONE;
    }
}

static void apply_recall(bl_live_t *e, const bl_state_t *snap, bl_tick_t tick)
{
    apply_recall_mask(e, snap, tick, (1u << e->project.nvoices) - 1u, 1, 1);
}

static int composer_owns(const bl_live_t *e, int i)
{
    const bl_vstate_t *s = &e->st[i];
    return e->project.composer.enabled && !(e->composer.manual_voices >> i & 1u) &&
        !s->voice.pat.locked && !s->voice.config_dirty && s->regen_req < 0 && s->blank_req < 0;
}

f64 bl_live_voice_density(const bl_live_t *e, int i)
{
    if (i < 0 || i >= e->project.nvoices) return ZERO;
    f64 density = e->density;
    if (f64_eq(density, ZERO)) return ZERO;
    int cut = e->st[i].voice.config.density_cut;
    if (cut) density = f64_mul(density, f64_div(f64_from_i64(100 - cut), F64(0x4059000000000000)));
    if (composer_owns(e, i)) {
        int level = bl_comp_level(&e->composer, e->st[i].voice.role);
        if (level < 100) density = f64_mul(density, f64_div(f64_from_i64(level), F64(0x4059000000000000)));
    }
    return density;
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
/* Returns whether new source material started here. Register/root/gate and
 * traversal-only edits retain the evolving motif and its independent streams. */
static int apply_voice_config(bl_live_t *e, int i, bl_tick_t tick)
{
    bl_voice_t *v = &e->st[i].voice;
    uint16_t mask = v->config_dirty;
    if (!mask) return 0;       /* manual configuration is independent of LOCK */
    int rebuild = (mask & ((1u << BL_CFG_GENERATOR) | (1u << BL_CFG_SCALE) |
                           (1u << BL_CFG_RATE) | (1u << BL_CFG_LOOP))) != 0;
    if (v->pending.gen.kind != BL_GEN_ARP && v->pending.gen.kind != BL_GEN_MOTIF &&
        (mask & (1u << BL_CFG_GATE))) rebuild = 1; /* legacy b is not a gate */
    v->config = v->pending;
    v->reg_low = v->config.reg_low;
    v->reg_high = v->config.reg_high;
    if (rebuild) {
        uint64_t seed = v->pat.seed;
        uint8_t locked = v->pat.locked;
        configured_pattern(&v->config, v->role, e->repeats, seed, &e->sched, &v->pat);
        v->pat.locked = locked;
        v->pat.start_offset = bl_floor_mod(-tick, v->pat.length);
        memset(&v->motif, 0, sizeof v->motif);
        if (v->pat.generator == BL_GEN_MOTIF)
            bl_motif_evolution_init(&v->motif, &v->config.key, seed, v->id);
        e->st[i].generation++;
    } else if (v->pat.generator == BL_GEN_ARP || v->pat.generator == BL_GEN_MOTIF) {
        if (mask & (1u << BL_CFG_GATE))
            for (int j = 0; j < v->pat.n; j++) v->pat.note[j].gate = v->config.gen.b;
        if (mask & (1u << BL_CFG_DIRECTION)) {
            v->pat.direction = v->config.gen.direction;
            v->pat.direction_cursor = 0;
        }
        v->pat.groove = v->config.gen.groove;
    }
    v->pat.pitch_phase = v->config.phase;
    e->project.voice[i].has_config = 1;
    e->project.voice[i].config = v->config;
    v->config_dirty = 0;
    return rebuild;
}

static void process_boundary(bl_live_t *e, int i, bl_tick_t tick)
{
    bl_vstate_t *s = &e->st[i];
    e->composer.requests &= ~(1u << i); /* this local completion consumes any Composer request */
    bl_pattern_t *p = &s->voice.pat;
    /* Timing-grid traversal continues through blank loops and LOCK. Neither is
     * a request to restart the pitch order. Explicit regeneration starts anew. */
    bl_direction_advance(p);
    char gen[24];
    int was_blank = s->blank;
    s->blank = 0;
    if (!was_blank) bl_advance_boundary(p);
    /* render_voice computed tick using the old length, so shortening only takes
     * effect after that entire cycle has completed. */
    int reconfigured = apply_voice_config(e, i, tick);
    if (s->blank_req >= 0 && tick >= s->blank_req) {
        s->blank_req = NONE;
        s->blank = 1;
        goto motif_boundary;
    }
    if (s->accent_req >= 0 && tick >= s->accent_req) {
        s->accent_req = NONE;
        if (!p->locked) {
            bl_dec(s->generation, gen);
            bl_move_accents(p, bl_seed_derive(e->project.seed, "voice", s->voice.id, "accentMove", gen,
                                              (const char *)NULL));
            p->remaining = (int16_t)(e->repeats - 1);
            s->generation++;
            goto motif_boundary;
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
            uint8_t direction = p->direction;
            uint8_t groove = p->groove;
            configured_pattern(&s->voice.config, s->voice.role, e->repeats, seed, &e->sched, p);
            if (p->generator == BL_GEN_ARP || p->generator == BL_GEN_MOTIF) {
                p->direction = direction < BL_NDIRS ? direction : BL_DIR_UP;
                p->groove = groove < BL_NGROOVES ? groove : BL_GROOVE_STRAIGHT;
            }
            if (p->generator == BL_GEN_MOTIF) {
                bl_motif_evolution_init(&s->voice.motif, &s->voice.config.key, seed, s->voice.id);
                p->start_offset = bl_floor_mod(-tick, p->length);
            }
            if (p->generator == BL_GEN_ARP &&
                (p->direction != BL_DIR_UP || p->groove != BL_GROOVE_STRAIGHT))
                p->start_offset = bl_floor_mod(-tick, p->length);
            if (e->project.voice[i].has_config) p->start_offset = bl_floor_mod(-tick, p->length);
            s->generation++;
            return;
        }
    }
    if (!reconfigured && bl_is_mutable(p)) {
        bl_dec(s->generation, gen);
        uint64_t seed = bl_seed_derive(e->project.seed, "voice", s->voice.id, "mut", gen, (const char *)NULL);
        bl_mutate(&s->voice, &s->voice.config.key, seed, e->repeats, project_ratchet(&e->project));
        s->generation++;
    }
motif_boundary:
    if (!was_blank && !reconfigured && p->generator == BL_GEN_MOTIF)
        bl_motif_evolution_boundary(p, &s->voice.motif, &s->voice.config.key);
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
                f64 density = bl_live_voice_density(e, i);
                int directed = (src->pat.generator == BL_GEN_ARP || src->pat.generator == BL_GEN_MOTIF)
                    && (src->pat.direction != BL_DIR_UP || src->pat.pitch_phase);
                if (directed || f64_lt(density, ONE)) {
                    e->capped = s->voice;
                    /* Map pitches before density removes onset slots; gates,
                     * accents and velocities still belong to their grid slots. */
                    if (directed) {
                        for (int j = 0; j < src->pat.n; j++)
                            e->capped.pat.note[j].pitch = bl_direction_pitch(&src->pat, j, 0);
                        e->capped.pat.direction = BL_DIR_UP;
                        e->capped.pat.direction_cursor = 0;
                        e->capped.pat.pitch_phase = 0;
                    }
                    bl_density_cap(&e->capped.pat, density, &e->capped.pat);
                    src = &e->capped;
                }
                int room = n < max ? max - n : 0;
                int k = bl_render_voice(src, (uint8_t)i, &e->sched, &s->voice.config.key, lower, upper,
                                        room ? out + n : out, room);
                for (int j = 0; j < k && j < room; j++) {
                    bl_note_t *q = &out[n + j];
                    /* The scratch voice has already mapped pitches and may
                     * have compacted its onset grid for density. Recover the
                     * original onset by tick, then retain its pitch source. */
                    if (directed && q->source_onset) {
                        for (int a = 0; a < s->voice.pat.n; a++) {
                            if (s->voice.pat.note[a].position + 1 != q->source_onset) continue;
                            int source = bl_direction_index(&s->voice.pat, a, 0);
                            q->source_onset = (uint32_t)(s->voice.pat.note[source].position + 1);
                            break;
                        }
                    }
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
        uint8_t midi = e->tension_note[k].midi;
        uint8_t channel = e->tension_note[k].channel;
        bl_delay_t delay = e->tension_note[k].delay;
        uint32_t source_onset = e->tension_note[k].source_onset;
        memmove(&e->tension_note[k], &e->tension_note[k + 1],
                (size_t)(e->ntension - k - 1) * sizeof e->tension_note[0]);
        e->ntension--;
        for (int i = 0; i < e->project.nvoices; i++) {
            if (bl_vid(e->project.voice[i].id) != vid || vid == BL_VID_NONE) continue;
            if (!f64_lt(ZERO, bl_live_voice_density(e, i))) break;
            if (n < max) out[n] = (bl_note_t){tick, BL_TICKS_PER_BAR, (uint8_t)i, midi, 110, 1, 1, delay, channel, source_onset};
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

static const bl_arrcfg_t *composer_form(const bl_live_t *e)
{
    return e->project.has_arr ? &e->project.arr : &bl_arr_classic;
}

static void composer_config_apply(bl_live_t *e, const bl_compcfg_t *cfg, bl_tick_t tick)
{
    int was = e->project.composer.enabled;
    e->project.composer = *cfg;
    if (cfg->enabled && was) {
        if (!cfg->evolution)
            for (int i = 0; i < e->project.nvoices; i++) composer_cancel_voice(e, i);
        e->composer.cfg = *cfg;           /* macros do not restart sections or phrase clocks */
        return;
    }
    if (!was && !cfg->enabled) return;
    for (int i = 0; i < e->project.nvoices; i++) composer_cancel_voice(e, i);
    uint8_t roles[BL_MAX_VOICES];
    uint32_t active = 0, manual = e->composer.manual_voices;
    uint8_t harmony = e->composer.manual_harmony, scene = e->composer.manual_scene;
    int legacy_on = was ? e->composer.legacy_enabled : e->has_arr;
    bl_arrcfg_t legacy_form = was ? e->composer.legacy_form :
        e->has_arr ? e->arr.cfg : e->project.has_arr ? e->project.arr : ARR_DEFAULT;
    arr_roles(&e->project, roles);
    for (int i = 0; i < e->project.nvoices; i++) if (e->st[i].active) active |= 1u << i;
    bl_arr_init(&e->arr, cfg->enabled ? composer_form(e) : &legacy_form, roles, e->project.nvoices,
        bl_seed_derive(e->project.seed, "arrangement", (const char *)NULL), 1, active, ambient_period(&e->project));
    e->has_theme = 0;                   /* each controller captures its own established theme */
    if (cfg->enabled) {
        bl_comp_init(&e->composer, cfg, composer_form(e), e->project.seed);
        bl_comp_select(&e->composer, e->project.composer_start);
        e->composer.started_at = tick;
        e->composer.manual_voices = manual;
        e->composer.manual_harmony = harmony;
        e->composer.manual_scene = scene;
        e->composer.legacy_enabled = (uint8_t)legacy_on;
        e->composer.legacy_form = legacy_form;
        e->has_arr = 1;
        e->arr.phase = e->composer.phase;
        e->arr.bars_in_phase = 0;
        e->arr.bars_until_change = e->composer.bars_left;
    } else {
        e->composer.cfg.enabled = 0;
        e->has_arr = (uint8_t)legacy_on;
        if (e->has_arr && e->arr.phase == BL_PH_BED) {
            bl_live_capture(e, &e->theme);
            e->has_theme = 1;
        }
    }
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
    case BL_A_KEY: {                    /* same boundary staging for any already-queued key change */
        bl_action_t a = {0};
        a.kind = BL_A_KEY;
        a.key = c->key;
        bl_live_enqueue(e, &a, c->tick);
        break;
    }
    case BL_A_DENSITY: e->density = clamp01(c->value); break;
    case BL_A_BPART: apply_bpart(e, c->tick); break;
    case BL_A_VOICE_ACTIVE:
        if ((s = state_of(e, c->voice))) {
            composer_manual_voice(e, (int)(s - e->st));
            s->active = c->on;
            if (c->on) {
                s->seg_start = c->tick;
                if (e->project.voice[(int)(s - e->st)].has_config || s->voice.pat.generator == BL_GEN_MOTIF ||
                    (s->voice.pat.generator == BL_GEN_ARP &&
                     (s->voice.pat.direction != BL_DIR_UP || s->voice.pat.groove != BL_GROOVE_STRAIGHT)))
                    s->voice.pat.start_offset = bl_floor_mod(-c->tick, s->voice.pat.length);
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
                e->tension_note[e->ntension].midi = 0;
                e->tension_note[e->ntension].channel = 0;
                e->tension_note[e->ntension].delay = bl_delay_config((bl_delay_t){0});
                e->tension_note[e->ntension].source_onset = 0;
                s = state_of(e, c->voice);
                if (s) {
                    e->tension_note[e->ntension].channel = s->voice.midi_channel;
                    e->tension_note[e->ntension].delay = s->voice.delay;
                    /* A one-shot is not a stored pattern note. Preserve its
                     * actual local event position before render-ahead moves
                     * or replaces this voice's source material. */
                    if (s->voice.pat.length > 0)
                        e->tension_note[e->ntension].source_onset = (uint32_t)
                            (bl_floor_mod(c->tick + s->voice.pat.start_offset, s->voice.pat.length) + 1);
                    bl_pitch_t pitch = {0, 0, 1};
                    e->tension_note[e->ntension].midi = bl_fold(s->voice.reg_low, s->voice.reg_high,
                        bl_resolve(&s->voice.config.key, pitch, s->voice.ref_c));
                }
                e->ntension++;
            }
        }
        break;
    case BL_A_SCENE: start_morph(e, scene_named(e, c->scene), c->bars, c->tick); break;
    case BL_A_COMPOSER: {
        uint32_t bits = (uint32_t)c->bars;
        bl_compcfg_t cfg = {(uint8_t)bits, (uint8_t)(bits >> 8), (uint8_t)(bits >> 16), (uint8_t)(bits >> 24)};
        composer_config_apply(e, &cfg, c->tick);
        break;
    }
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
            if (e->project.voice[d[k].voice].has_config || s->voice.pat.generator == BL_GEN_MOTIF ||
                (s->voice.pat.generator == BL_GEN_ARP &&
                 (s->voice.pat.direction != BL_DIR_UP || s->voice.pat.groove != BL_GROOVE_STRAIGHT)))
                s->voice.pat.start_offset = bl_floor_mod(-tick, s->voice.pat.length);
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
static void composer_section(bl_live_t *e, bl_tick_t tick)
{
    const bl_composer_t *c = &e->composer;
    uint32_t owned = 0;
    int protected = c->manual_harmony != 0, locked = 0;
    for (int i = 0; i < e->project.nvoices; i++) {
        if (composer_owns(e, i)) owned |= 1u << i;
        if (e->st[i].voice.pat.locked) locked = protected = 1;
    }
    if (c->phase == BL_PH_BED && !e->has_theme) {
        bl_live_capture(e, &e->theme);
        e->has_theme = 1;
    }
    if (c->phase == BL_PH_PAYOFF && e->has_theme) {
        for (int i = 0; i < e->project.nvoices; i++) composer_cancel_voice(e, i);
        apply_recall_mask(e, &e->theme, tick, owned, !protected, 0);
    }
    if (c->phase == BL_PH_BPART && c->cfg.tension && !protected) {
        /* One shared, scale-degree harmonic move; bass and upper voices use
         * the same schedule. No private clock, rate change or random reseed. */
        int32_t bar = head_bar(e);
        int base = e->has_theme ? bl_sched_offset(&e->theme.sched, bar) : bl_sched_offset(&e->sched, bar);
        int lift = c->cfg.tension == 1 ? 3 : c->cfg.tension == 2 ? 4 : 5;
        if (base >= -128 && base <= 127 - lift) {
            bl_tev_t ev = {bar, base + lift};
            (void)bl_sched_merge(&e->sched, &ev, 1, bar); /* full schedule: retain the existing harmony */
        }
    }
    if (!c->manual_scene && !locked) start_morph(e, e->project.scene_by_phase[c->phase], 8, tick);
}

static void composer_development(bl_live_t *e, bl_tick_t tick)
{
    const bl_composer_t *c = &e->composer;
    if (!c->cfg.evolution || (c->phase != BL_PH_VARIATION && c->phase != BL_PH_BPART) || !e->project.nvoices) return;
    /* At most one supporting melodic voice per phrase checkpoint. Bass,
     * sustained support and root anchors retain their existing material. */
    int n = e->project.nvoices;
    int start = (int)((c->cycle % (uint32_t)n + (uint32_t)c->bars_in_section / 8u) % (uint32_t)n);
    for (int k = 0; k < n; k++) {
        int i = (start + k) % n;
        bl_vstate_t *s = &e->st[i];
        int role = s->voice.role;
        if (!composer_owns(e, i) || !s->active || s->accent_req >= 0 || s->nudge_req >= 0 ||
            (role != BL_ROLE_BED1 && role != BL_ROLE_BED2 && role != BL_ROLE_LEAD)) continue;
        /* Existing local-boundary accent/mutation paths preserve groove,
         * direction, gate, motif anchors and independent completion clocks. */
        if (c->cfg.evolution == 3 && s->voice.pat.generator != BL_GEN_ARP && s->voice.pat.generator != BL_GEN_MOTIF)
            s->nudge_req = tick;
        else s->accent_req = tick;
        e->composer.requests |= 1u << i;
        break;
    }
}

static void composer_boundary(bl_live_t *e, bl_tick_t tick, int created)
{
    int changed = 0, advanced = 0;
    if (e->composer.pending_stage && e->composer.pending_tick <= tick) {
        int section = e->composer.pending_stage - 1;
        e->composer.pending_stage = 0;
        bl_comp_select(&e->composer, section);
    }
    int selected = e->composer.enter_pending;
    if (selected) {
        e->composer.enter_pending = 0;
        for (int i = 0; i < e->project.nvoices; i++) composer_cancel_voice(e, i);
        composer_section(e, tick);
        changed = 1;
    }
    /* Explicit selection wins this bar. Preserve queued Advance requests
     * for the next running bar, rather than overriding the selected target. */
    while (e->has_arr && !selected && e->nadv && e->adv[0] <= tick) {
        e->nadv--;
        memmove(&e->adv[0], &e->adv[1], (size_t)e->nadv * sizeof e->adv[0]);
        (void)bl_comp_tick(&e->composer, composer_form(e), 1);
        composer_section(e, tick);
        changed = advanced = 1;
    }
    if (e->has_arr && !selected && !advanced && !created && tick > e->composer.started_at) {
        changed = bl_comp_tick(&e->composer, composer_form(e), 0);
        if (changed) composer_section(e, tick);
    }
    if (changed || (e->has_arr && e->composer.cfg.evolution >= 2 && e->composer.bars_in_section &&
        !(e->composer.bars_in_section % 8))) composer_development(e, tick);
    e->arr.phase = e->composer.phase;
    e->arr.bars_in_phase = e->composer.bars_in_section;
    e->arr.bars_until_change = e->composer.bars_left;
    e->arr.nactive = 0;
    for (int i = 0; i < e->project.nvoices; i++) if (e->st[i].active) e->arr.active[e->arr.nactive++] = (uint8_t)i;
}

static void arr_boundary(bl_live_t *e, bl_tick_t tick)
{
    bl_dir_t d[BL_ARR_MAX_DIRS];
    if (tick < e->arr_cursor) return;
    e->arr_cursor = tick + 1;
    int created = 0;
    while (e->nen && e->en[0].tick <= tick) {
        uint8_t on = e->en[0].on;
        if (e->project.composer.enabled) {
            e->composer.legacy_enabled = on;
            e->composer.legacy_form.enabled = on;
        }
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
    /* In Composer mode, already queued manual changes bind first. Legacy
     * AUTO retains its original ordering when Composer is disabled. */
    int composer_change = e->project.composer.enabled;
    for (int i = 0; i < e->nchange && e->change[i].tick <= tick; i++)
        if (e->change[i].kind == BL_A_COMPOSER) composer_change = 1;
    if (composer_change) {
        while (e->nchange && e->change[0].tick <= tick) {
            bl_change_t c = e->change[0];
            int was_composer = e->project.composer.enabled;
            e->nchange--;
            memmove(&e->change[0], &e->change[1], (size_t)e->nchange * sizeof c);
            apply_change(e, &c);
            if (c.kind == BL_A_COMPOSER && was_composer != e->project.composer.enabled) created = 1;
        }
    }
    if (e->project.composer.enabled) {
        composer_boundary(e, tick, created);
        return;                         /* exactly one automation owner */
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

const bl_voice_t *bl_live_voice(const bl_live_t *e, int i) { return &e->st[i].voice; }

/* ------------------------------------------------------------ snapshot --- */
const char *const BL_UP_RAW[BL_NUPS] = {"key", "recall", "density", "bPart", "voiceActive", "tension", "arranger",
                                        "advance", "scene", "composer"};

static const uint8_t UP_OF_CHANGE[] = {
    [BL_A_RECALL] = BL_UP_RECALL, [BL_A_KEY] = BL_UP_KEY, [BL_A_DENSITY] = BL_UP_DENSITY,
    [BL_A_BPART] = BL_UP_BPART, [BL_A_VOICE_ACTIVE] = BL_UP_VOICE_ACTIVE, [BL_A_TENSION] = BL_UP_TENSION,
    [BL_A_SCENE] = BL_UP_SCENE,
    [BL_A_COMPOSER] = BL_UP_COMPOSER,
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
    out->composing = e->project.composer.enabled;
    out->composition_cycle = e->composer.cycle;
    out->pending_section = e->composer.pending_stage;
    out->manual_voices = e->composer.manual_voices;
    if (e->has_arr || out->composing) {
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
        case BL_UP_COMPOSER:
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
