/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan's project on the FM-1 (app.h): the app's project as AppServices keeps it, and what SAVE writes, as
 * AppServices.saveDocument does: the project (OBJ_PROJ) and the six snapshot slots (OBJ_SLOT0..5, each a state as
 * state.h packs it, behind a header of its own). A project record of another magic or format, a short one, one with
 * a value out of its range: the defaults, the Demo trio in A minor as the Mac's launch (its seed drawn at the first
 * Play, template or Dice: at boot the clock and the pot hardly differ from one power-on to the next); a slot record
 * that is not one: an empty slot. The undo stack and the running engine are not saved. Part of the unity build,
 * after runtime.c.
 *
 * As on the Mac (AppServices, UIState):
 * - The project is the app's; the transport runs a copy of its own (Transport.project). Stopped, a change rebuilds
 *   the transport on the project (proj_apply: the next Play's engine, no mutes, the tempo); playing, the part the Mac
 *   does live goes to the running engine (tempo, key, density, the arranger's on / off at the next bar, a channel at
 *   once, a template at the next bar line, Dice's fresh state recalled at the next bar) and the project waits for
 *   the stop (transportStale, rebuildTransportIfStale). The arranger's form and ambient mode are stopped only.
 *   Clock source and external input routing belong to device_settings, never the project record.
 * - The ratchet (the legitimization's ratchetProbability) is a project value with no live action on the Mac (which
 *   has no control for it): playing, a change waits for the next Play, as the stale project's others.
 * - The arrangement is the Mac's: nil or a config, with AUTO (enabled), the form (a preset's periods and reprise) and
 *   ambient mode in it apart; AUTO or ambient mode on a nil one starts from ArrangementConfig(); off keeps the rest.
 * - A slot stores the transport's state at the render head (captureState: before a Play of the project in place,
 *   its initial state). Recall and undo enqueue a recall, which a stopped transport drops (Play starts with no
 *   actions). Undo: a point before every destructive action, 20 deep, the oldest dropped; a load empties it
 *   (loadSlots).
 * Where it is the FM-1's own:
 * - SAVE while playing waits for the stop (proj_stopped: the button's, MIDI Stop, the arranger's ending): a flash
 *   write stops everything for a moment, the MIDI clock too. It is written once what the stop sends (MIDI Stop, the
 *   offs) has gone out (rt_idle), on a later frame.
 * - A stale project reaches the transport after any stop, not only the PLAY button's (the Mac's rebuild).
 * - A channel is the template's voice i's (the UI's voice i).
 * - The slots and the undo points are kept packed (a bl_state_t is 16.8 KB), the undo points in 32 KB: 20 of the
 *   templates' states fit with room to spare, a larger state takes the room of more than one. */
#include <stddef.h>
#include "state.h"

/* The BLE shim provides these symbols in a separate translation unit.
 * Use the platform ABI (32-bit unsigned length), not the app engine\'s
 * freestanding string.h, whose size_t declarations conflict with the BSP. */
#if FM1B_BLE && !defined(PLAT_HOST)
int strcmp(const char *a, const char *b);
void *memmove(void *d, const void *s, unsigned n);
#endif

#define SLOT_MAGIC 0x31534C42u           /* "BLS1" */
#define SLOT_FORMAT 9u                  /* per-voice active/pending density, reads format 8 */
#define SLOT_HEAD 8                      /* magic, format */
#define STATE_MAX ((int)PLAT_STORE_MAX - SLOT_HEAD)
#define HUNDRED F64(0x4059000000000000)  /* 100.0 */
#define UNDO_BYTES 32768                 /* 20 undo points of 1,636 bytes (the templates' states: 800 or less); larger
                                          * ones leave room for fewer */

proj_t proj;

static struct {
    uint8_t used, dirty;                 /* stored; stored since the last SAVE or load */
    uint16_t n;                          /* the record's bytes */
    uint8_t rec[PLAT_STORE_MAX];         /* the store object: the header, the state */
} slots[PROJ_SLOTS] PLAT_POOL;
static uint8_t undo_mem[UNDO_BYTES] PLAT_POOL;   /* the undo points, the oldest first: each its u16 length, its
                                                  * bytes */
static uint16_t undo_used;               /* bytes */
static uint8_t undo_n;
static uint8_t undo_lost;                /* the last push took no point (undo_refused) */
static uint8_t proj_stale;               /* transportStale */
static uint8_t proj_unseeded;            /* the defaults: their seed is drawn at the first Play */
static uint8_t proj_saving;              /* a SAVE made while playing, written at the stop */
static uint8_t proj_rt_ratchet;          /* the ratchet of the project the transport was last given */
static uint8_t proj_template_pending;    /* a new template requested but not yet observed in the engine */
static bl_state_t proj_st PLAT_POOL;     /* a state captured, unpacked */
static bl_project_t proj_eng PLAT_POOL;  /* the project as the engine's, a template */
static uint8_t proj_buf[PLAT_STORE_MAX] PLAT_POOL;   /* a state packed; a record read back */
#ifdef PLAT_HOST
static void host_saving(void);           /* host.c: a test's look at the transport as a SAVE writes */
#endif

static const bl_arrcfg_t *const FORMS[] = {0, &bl_arr_classic, &bl_arr_slow, &bl_arr_ambient};
static const bl_arrcfg_t ARR_DEFAULT = {0, 0, 0, 8, 16};      /* ArrangementConfig() */

uint64_t proj_fresh_seed(void)           /* one SplitMix64 round over the ms and the MASTER pot */
{
    bl_rng_t r;
    bl_rng_seed(&r, (uint64_t)plat_ms() << 32 | plat_master());
    return bl_rng_next(&r);
}

static void arr_get(bl_arrcfg_t *c)     /* the project's arrangement config (nil: zeros) */
{
    c->enabled = proj.arr_on;
    c->ambient = proj.arr_ambient;
    c->reprise = proj.arr_reprise;
    c->min_bars = proj.arr_min;
    c->max_bars = proj.arr_max;
}

static void arr_set(int has, const bl_arrcfg_t *c)
{
    proj.arr_has = (uint8_t)(has != 0);
    proj.arr_on = has ? c->enabled : 0;
    proj.arr_ambient = has ? c->ambient : 0;
    proj.arr_reprise = has ? c->reprise : 0;
    proj.arr_min = has ? c->min_bars : 0;
    proj.arr_max = has ? c->max_bars : 0;
}

static void arr_or_default(void)         /* project.arrangement ?? ArrangementConfig() */
{
    if (!proj.arr_has)
        arr_set(1, &ARR_DEFAULT);
}

int proj_form(void)
{
    int f;
    for (f = FORM_CLASSIC; proj.arr_has && f <= FORM_AMBIENT; f++)
        if (proj.arr_ambient == FORMS[f]->ambient && proj.arr_reprise == FORMS[f]->reprise
            && proj.arr_min == FORMS[f]->min_bars && proj.arr_max == FORMS[f]->max_bars)
            return f;
    return FORM_OTHER;
}

static void from_template(int t, uint64_t seed)   /* AppServices.applyTemplate: the template, as it is, on seed */
{
    int i;
    bl_template(t, &proj_eng);
    proj.tpl = (uint8_t)t;
    proj.key_root = (uint8_t)proj_eng.key.root;
    proj.key_mode = proj_eng.key.scale.mode;
    proj.bpm = proj_eng.bpm;
    arr_set(proj_eng.has_arr, &proj_eng.arr);   /* its config, whatever it is */
    proj.density_pct = 100;
    proj.has_density = 0;
    proj.composer = (bl_compcfg_t){0, 0, 1, 1};
    proj.composer_start = 0;
    memset(proj.channel, 0, sizeof proj.channel);
    memset(proj.voice_configured, 0, sizeof proj.voice_configured);
    memset(proj.voice_config, 0, sizeof proj.voice_config);
    /* A fresh template must not inherit the preceding template's LFO
     * targets or mixer CCs, just as it does not inherit voice configs. */
    memset(proj.lfo_override, 0, sizeof proj.lfo_override);
    memset(proj.lfo_target, 0, sizeof proj.lfo_target);
    memset(proj.lfo_cycle, 0, sizeof proj.lfo_cycle);
    memset(proj.lfo_depth, 0, sizeof proj.lfo_depth);
    memset(proj.lfo_center, 0, sizeof proj.lfo_center);
    memset(proj.mixer_mask, 0, sizeof proj.mixer_mask);
    memset(proj.mixer_value, 0, sizeof proj.mixer_value);
    for (i = 0; i < BL_MAX_VOICES; i++) proj.delay[i] = bl_delay_config((bl_delay_t){0});
    for (i = 0; i < proj_eng.nvoices; i++)
        proj.channel[i] = proj_eng.voice[i].channel;
    proj.seed = seed;
}

void proj_defaults(void)
{
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    from_template(BL_TPL_DEMO, 0);
    proj.seed = proj_eng.seed;           /* (the template's own until the first Play draws one) */
    proj_unseeded = 1;
}

/* In-memory musical configuration, after record decoding. */
static int proj_valid(const proj_t *p)
{
    int i;
    if (p->magic != PROJ_MAGIC || p->format != PROJ_FORMAT)
        return 0;
    if (p->ratchet_pct > 50 || p->ratchet_pct % 5)
        return 0;
    if (p->tpl >= BL_NTPL || p->key_root > 11 || p->key_mode >= BL_NMODES
        || p->composer_start > 6 || p->bpm < 40 || p->bpm > 220 || p->density_pct > 100 || p->has_density > 1)
        return 0;
    if (!bl_comp_valid(&p->composer) || p->arr_has > 1 || p->arr_on > 1 || p->arr_ambient > 1 || p->arr_reprise > 1)
        return 0;
    if (p->arr_has ? p->arr_min < 1 || p->arr_min > p->arr_max || p->arr_max > 1024
                   : p->arr_on || p->arr_ambient || p->arr_reprise || p->arr_min || p->arr_max)
        return 0;
    for (i = 0; i < BL_MAX_VOICES; i++) {
        if (p->channel[i] > 15 || !bl_delay_valid(p->delay[i]) || p->voice_configured[i] > 1 ||
            (p->voice_configured[i] && !bl_config_valid(&p->voice_config[i])))
            return 0;
        if (p->lfo_override[i] > 1 || p->lfo_target[i] >= LFO_NDEST ||
            p->lfo_cycle[i] >= LFO_NCYCLES || p->lfo_depth[i] > 100 || p->lfo_center[i] > 100 ||
            p->mixer_mask[i] > 15) return 0;
        for (int k = 0; k < MIDI_MIX_N; k++) if (p->mixer_value[i][k] > 127) return 0;
    }
    return 1;
}

#include "project_record.c"              /* explicit format 8, readers for formats 5, 6 and 7 */

static void slot_load(int k)             /* the slot's record, if it is one (its state unpacks) */
{
    uint32_t head[2];
    int n = plat_store_load(OBJ_SLOT0 + (uint32_t)k, slots[k].rec, PLAT_STORE_MAX);
    slots[k].used = slots[k].dirty = 0;
    slots[k].n = 0;
    if (n <= SLOT_HEAD)
        return;
    memcpy(head, slots[k].rec, sizeof head);
    if (head[0] != SLOT_MAGIC || (head[1] != SLOT_FORMAT && head[1] != 8)
        || state_unpack_format(slots[k].rec + SLOT_HEAD, n - SLOT_HEAD, &proj_st, (int)head[1]))
        return;
    slots[k].used = 1;
    slots[k].n = (uint16_t)n;
}

int proj_load(void)
{
    proj.composer_start = 0;
    static proj_t rec;
    int n, k, legacy_lights = -1;
    undo_used = undo_n = 0;
    proj_stale = proj_saving = 0;
    proj_template_pending = 0;
    for (k = 0; k < PROJ_SLOTS; k++)
        slot_load(k);
    memset(&rec, 0, sizeof rec);
    n = plat_store_load(OBJ_PROJ, proj_buf, sizeof proj_buf);
    if (!project_record_read(proj_buf, n, &rec, &legacy_lights)) {
        if (legacy_lights >= 0) device_settings_import_lights(legacy_lights);
        proj = rec;
        proj_unseeded = 0;
        return 0;
    }
    proj_defaults();
    return -1;
}

static int save_obj(uint32_t obj, const void *src, uint32_t n)   /* written, and read back the same */
{
    if (plat_store_save(obj, src, n))
        return -1;
    return plat_store_load(obj, proj_buf, sizeof proj_buf) == (int)n && !memcmp(proj_buf, src, n) ? 0 : -1;
}

static void draw_seed(void)              /* the defaults' seed, now (the Mac draws it at each launch) */
{
    if (!proj_unseeded)
        return;
    proj.seed = proj_fresh_seed();
    proj_unseeded = 0;
    proj_stale = 1;
}

int proj_save_pending(void) { return proj_saving; }
int proj_seeded(void) { return !proj_unseeded; }
static void sync_voice_configs(void);

int proj_save(void)                      /* the project; the slots stored since (the others are on the flash) */
{
    int k, r;
    if (rt_playing()) {
        proj_saving = 1;
        return 1;
    }
    sync_voice_configs();
    proj_saving = 0;
    int lights_error = device_settings_preserve_lights();
#ifdef PLAT_HOST
    host_saving();
#endif
    draw_seed();                         /* (a project saved has its seed) */
    uint8_t record[PROJECT_RECORD_BYTES];
    int bytes = project_record_write(&proj, record, sizeof record);
    r = lights_error || bytes < 0 ? -1 : save_obj(OBJ_PROJ, record, (uint32_t)bytes);
    for (k = 0; k < PROJ_SLOTS; k++)
        if (slots[k].dirty) {
            if (save_obj(OBJ_SLOT0 + (uint32_t)k, slots[k].rec, slots[k].n))
                r = -1;
            else
                slots[k].dirty = 0;
        }
    return r;
}

static f64 pct_of(int pct) { return f64_div(f64_from_i64(pct), HUNDRED); }   /* Double(pct) / 100 */

/* Existing template lane defaults are preserved until a voice is edited.
 * Present templates: DEMO/FULL bedPluck1 has filter 16 bars, 30% / 60%;
 * FULL pad has filter 32 bars, 40% / 50%. Other voices have no lane. */
int proj_lfo_get(int voice, int *target, int *cycle, int *depth, int *center)
{
    if (voice < 0 || voice >= BL_MAX_VOICES || !target || !cycle || !depth || !center) return -1;
    if (proj.lfo_override[voice]) {
        *target = proj.lfo_target[voice];
        *cycle = proj.lfo_cycle[voice];
        *depth = proj.lfo_depth[voice];
        *center = proj.lfo_center[voice];
    } else {
        *target = LFO_DEST_OFF, *cycle = 3, *depth = 30, *center = 50;
        if ((proj.tpl == BL_TPL_DEMO || proj.tpl == BL_TPL_FULL) && voice == 0) {
            *target = LFO_DEST_CUTOFF, *cycle = 5, *depth = 30, *center = 60;
        } else if (proj.tpl == BL_TPL_FULL && voice == 3) {
            *target = LFO_DEST_CUTOFF, *cycle = 6, *depth = 40, *center = 50;
        }
    }
    return 0;
}

/* CC map slot 0 belongs to template scenes. Editable sine LFOs use slot 1
 * to preserve existing scene morph targets and their own CC74 mapping. */
void proj_lfo_apply(bl_vcfg_t *cfg, int voice)
{
    if (!cfg || voice < 0 || voice >= BL_MAX_VOICES || !proj.lfo_override[voice]) return;
    if (proj.lfo_target[voice] == LFO_DEST_OFF) {
        cfg->nlanes = 0;
        return;
    }
    static const uint8_t number[LFO_NDEST] = {0, 1, 11, 10, 71, 74, 75};
    static const char *const name[LFO_NDEST] =
        {"none", "mod", "expression", "pan", "resonance", "filter", "decay"};
    int target = proj.lfo_target[voice], cycle = proj.lfo_cycle[voice];
    cfg->ncc = 2;
    cfg->cc_num[1] = number[target];
    cfg->cc_name[1] = name[target];
    cfg->nlanes = 1;
    cfg->lane[0] = (bl_cclane_t){
        BL_LFO_SINE, 1,
        cycle ? f64_from_i64((int64_t)1 << (cycle - 1)) : F64(0x3FE0000000000000),
        pct_of(proj.lfo_depth[voice]), pct_of(proj.lfo_center[voice]),
        F64(0x0000000000000000)
    };
}

int proj_lfo_set(int voice, int target, int cycle, int depth, int center)
{
    if (voice < 0 || voice >= BL_MAX_VOICES || target < 0 || target >= LFO_NDEST ||
        cycle < 0 || cycle >= LFO_NCYCLES || depth < 0 || depth > 100 || center < 0 || center > 100 ||
        rt_loading()) return -1;
    uint8_t original[5] = {proj.lfo_override[voice], proj.lfo_target[voice],
        proj.lfo_cycle[voice], proj.lfo_depth[voice], proj.lfo_center[voice]};
    proj.lfo_override[voice] = 1;
    proj.lfo_target[voice] = (uint8_t)target;
    proj.lfo_cycle[voice] = (uint8_t)cycle;
    proj.lfo_depth[voice] = (uint8_t)depth;
    proj.lfo_center[voice] = (uint8_t)center;
    /* Runtime edits the existing live CC descriptor, never the song engine.
     * It affects the next CC window, while existing scheduled CCs drain. */
    bl_vcfg_t cfg = {0};
    proj_lfo_apply(&cfg, voice);
    if (rt_set_lfo(voice, &cfg)) {
        proj.lfo_override[voice] = original[0], proj.lfo_target[voice] = original[1];
        proj.lfo_cycle[voice] = original[2], proj.lfo_depth[voice] = original[3];
        proj.lfo_center[voice] = original[4];
        return -1;
    }
    proj_stale = 1;
    return 0;
}

int proj_mixer_value(int voice, int control)
{
    static const uint8_t initial[MIDI_MIX_N] = {100, 64, 64, 64};
    if (voice < 0 || voice >= BL_MAX_VOICES || control < 0 || control >= MIDI_MIX_N) return -1;
    return proj.mixer_mask[voice] & (1u << control) ?
        proj.mixer_value[voice][control] : initial[control];
}

int proj_mixer_set(int voice, int control, int value)
{
    static const uint8_t cc[MIDI_MIX_N] = {7, 10, 74, 71};
    if (voice < 0 || voice >= BL_MAX_VOICES || control < 0 || control >= MIDI_MIX_N ||
        value < 0 || value > 127 || rt_loading()) return -1;
    /* Apply to the currently selected MIDI channel immediately, including
     * while STOPPED. With shared channels this controls every stacked part. */
    if (rt_send_channel_cc(proj.channel[voice], cc[control], (uint8_t)value)) return -1;
    proj.mixer_mask[voice] |= (uint8_t)(1u << control);
    proj.mixer_value[voice][control] = (uint8_t)value;
    proj_stale = 1;
    return 0;
}

/* the template in A minor, then the project's own values; the legitimization Legitimizer.Config(repeats: 4,
 * ratchetProbability: Double(pct) / 100): the FM-1's project always has one (0: no ratchets) */
void proj_to_engine(bl_project_t *out)
{
    int i;
    bl_template(proj.tpl, out);
    out->seed = proj.seed;
    out->key.root = proj.key_root;
    bl_scale_preset(proj.key_mode, &out->key.scale);
    out->bpm = proj.bpm;
    out->has_arr = proj.arr_has;
    arr_get(&out->arr);
    out->composer = proj.composer;
    out->composer_start = proj.composer_start;
    if (proj.has_density)
        out->density = pct_of(proj.density_pct);
    out->has_ratchet = 1;
    out->ratchet_p = pct_of(proj.ratchet_pct);
    for (i = 0; i < out->nvoices; i++) {
        out->voice[i].channel = proj.channel[i];
        out->voice[i].delay = bl_delay_config(proj.delay[i]);
        out->voice[i].mixer_mask = proj.mixer_mask[i];
        memcpy(out->voice[i].mixer_value, proj.mixer_value[i], MIDI_MIX_N);
        proj_lfo_apply(&out->voice[i], i);
        out->voice[i].has_config = proj.voice_configured[i];
        if (proj.voice_configured[i]) out->voice[i].config = proj.voice_config[i];
    }
}

void proj_apply(void)                    /* rebuildTransport: a stopped transport takes the project */
{
    if (rt_playing()) {
        proj_stale = 1;
        return;
    }
    proj_to_engine(&proj_eng);
    rt_load(&proj_eng);
    proj_template_pending = 0;
    proj_rt_ratchet = proj.ratchet_pct;
    proj_stale = 0;
}

void proj_ready(void)
{
    draw_seed();
    if (proj_stale)
        proj_apply();
}

/* Recall/undo can change engine targets without touching app controls. Carry
 * those frames into the saved project on stop, including still-pending edits.
 * A different template awaiting activation keeps its own requested defaults. */
static void sync_voice_configs(void)
{
    if (!rt_has_voice_state()) return;
    if (proj_template_pending) {
        if (rt_engine()->project.seed != proj.seed) return;
        proj_template_pending = 0;
    }
    rt_capture(&proj_st);
    proj_to_engine(&proj_eng);
    if (proj_st.n != proj_eng.nvoices) return;
    for (int i = 0; i < proj_st.n; i++)
        if (strcmp(proj_st.v[i].id, proj_eng.voice[i].id)) return;
    /* Reuse the existing scratch state to materialize implicit defaults once.
     * Keep only captured persistence targets on the stack, not full patterns. */
    struct { bl_vconfig_t config; bl_delay_t delay; uint8_t channel; } captured[BL_MAX_VOICES];
    int n = proj_st.n, need_defaults = 0;
    for (int i = 0; i < n; i++) {
        captured[i].config = proj_st.v[i].pending;
        captured[i].delay = proj_st.v[i].delay;
        captured[i].channel = proj_st.v[i].midi_channel;
        need_defaults |= !proj.voice_configured[i];
    }
    if (need_defaults && bl_initial_state(&proj_eng, &proj_st)) return;
    for (int i = 0; i < n; i++) {
        const bl_vconfig_t *target = &captured[i].config;
        if (!bl_config_valid(target)) continue;
        const bl_vconfig_t *effective = proj.voice_configured[i] ? &proj.voice_config[i] : &proj_st.v[i].config;
        int changed = bl_config_diff(effective, target) != 0;
        if (!proj.voice_configured[i] || bl_config_diff(&proj.voice_config[i], target)) {
            proj.voice_configured[i] = 1;
            proj.voice_config[i] = *target;
            if (changed) proj_stale = 1; /* explicit promotion alone does not reload transport */
        }
        if (memcmp(&proj.delay[i], &captured[i].delay, sizeof proj.delay[i]) ||
            proj.channel[i] != captured[i].channel) {
            proj.delay[i] = captured[i].delay;
            proj.channel[i] = captured[i].channel;
            proj_stale = 1;
        }
    }
}

int proj_stopped(void)
{
    int r = 1;
    if (rt_playing())
        return 1;
    sync_voice_configs();
    if (proj_saving && rt_idle())        /* (the stop's Stop and offs out first: the write would hold them) */
        r = proj_save();
    if (proj_stale)
        proj_apply();
    return r;
}

/* ---- AppServices' changes */
static int proj_recall(const bl_state_t *s)   /* .recallSnapshot(s), at the next bar */
{
    bl_action_t a;
    memset(&a, 0, sizeof a);
    a.kind = BL_A_RECALL;
    a.state = s;
    return rt_action(&a);
}

static int template_seeded(int t, uint64_t seed)
{
    if (t < 0 || t >= BL_NTPL)
        return -1;
    from_template(t, seed);
    proj_unseeded = 0;
    if (!rt_playing()) {
        proj_apply();
        return 0;
    }
    proj_to_engine(&proj_eng);
    rt_load(&proj_eng);                  /* Transport.loadProject: from the next bar line */
    proj_template_pending = 1;
    proj_rt_ratchet = proj.ratchet_pct;
    proj_stale = 1;
    return 0;
}

int proj_set_template(int tpl) { return template_seeded(tpl, proj_fresh_seed()); }

int proj_get_voice_config(int voice, bl_vconfig_t *target)
{
    if (!target || rt_loading()) return -1;
    proj_to_engine(&proj_eng);
    if (voice < 0 || voice >= proj_eng.nvoices) return -1;
    if (rt_playing()) return rt_get_voice_config(bl_vid(proj_eng.voice[voice].id), NULL, target);
    if (bl_initial_state(&proj_eng, &proj_st)) return -1;
    *target = proj_st.v[voice].pending;
    return 0;
}

int proj_set_voice_config(int voice, const bl_vconfig_t *target)
{
    if (!target || !bl_config_valid(target) || rt_loading()) return -1;
    proj_to_engine(&proj_eng);
    if (voice < 0 || voice >= proj_eng.nvoices) return -1;
    if (rt_playing() && rt_set_voice_config(bl_vid(proj_eng.voice[voice].id), target)) return -1;
    proj.voice_configured[voice] = 1;
    proj.voice_config[voice] = *target;
    proj_stale = 1;
    if (!rt_playing()) proj_apply();
    return 0;
}

int proj_set_voice_config_field(int voice, int field, int32_t value)
{
    bl_vconfig_t target;
    if (proj_get_voice_config(voice, &target) || bl_config_edit(&target, field, value)) return -1;
    return proj_set_voice_config(voice, &target);
}

int proj_composer_start(void) { return proj.composer_start; }

int proj_select_composer_stage(int section)
{
    if (section < 0 || section >= 7 || rt_loading()) return -1;
    if (rt_select_composer_stage(section)) return -1;
    if (rt_playing()) return 0;
    proj.composer_start = (uint8_t)section;
    return 0;
}

int proj_set_composer(const bl_compcfg_t *cfg)
{
    if (!cfg || !bl_comp_valid(cfg) || rt_loading()) return -1;
    if (rt_playing() && rt_set_composer(cfg)) return -1;
    proj.composer = *cfg;
    proj_stale = 1;
    if (!rt_playing()) proj_apply();
    return 0;
}

int proj_set_key(int root, int mode)
{
    bl_action_t a;
    if (root < 0 || root > 11 || mode < 0 || mode >= BL_NMODES)
        return -1;
    if (!rt_playing()) {
        proj.key_root = (uint8_t)root;
        proj.key_mode = (uint8_t)mode;
        for (int i = 0; i < BL_MAX_VOICES; i++) if (proj.voice_configured[i]) {
            proj.voice_config[i].key.root = root;
            bl_scale_preset(mode, &proj.voice_config[i].key.scale);
        }
        proj_apply();
        return 0;
    }
    if (root == proj.key_root && mode == proj.key_mode)   /* (the key it has: no change) */
        return 0;
    proj.key_root = (uint8_t)root;
    proj.key_mode = (uint8_t)mode;
    for (int i = 0; i < BL_MAX_VOICES; i++) if (proj.voice_configured[i]) {
        proj.voice_config[i].key.root = root;
        bl_scale_preset(mode, &proj.voice_config[i].key.scale);
    }
    proj_stale = 1;
    memset(&a, 0, sizeof a);
    a.kind = BL_A_KEY;
    a.key.root = root;
    bl_scale_preset(mode, &a.key.scale);
    return rt_action(&a);
}

void proj_set_bpm(int32_t bpm)           /* 40-220, the Mac's stepper */
{
    bpm = bpm < 40 ? 40 : bpm > 220 ? 220 : bpm;
    if (!rt_playing()) {
        proj.bpm = bpm;
        proj_apply();
        return;
    }
    if (bpm == proj.bpm)
        return;
    proj.bpm = bpm;
    proj_stale = 1;
    rt_set_tempo(bpm);
}

int proj_set_density(int pct)
{
    bl_action_t a;
    pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    proj.density_pct = (uint8_t)pct;
    proj.has_density = 1;
    if (!rt_playing()) {
        proj_apply();
        return 0;
    }
    proj_stale = 1;
    memset(&a, 0, sizeof a);
    a.kind = BL_A_DENSITY;
    a.value = pct_of(pct);
    return rt_action(&a);
}

int proj_set_arranger(int on)            /* (arrangement ?? ArrangementConfig()), enabled on; the form kept */
{
    bl_action_t a;
    arr_or_default();
    proj.arr_on = on ? 1 : 0;
    if (!rt_playing()) {
        proj_apply();
        return 0;
    }
    proj_stale = 1;
    memset(&a, 0, sizeof a);
    a.kind = BL_A_ARRANGER;
    a.arg = proj.arr_on;
    return rt_action(&a);
}

int proj_set_form(int form)              /* change Composer form at the next section, never restart playback */
{
    if (form < FORM_CLASSIC || form > FORM_AMBIENT) return -1;
    if (rt_playing()) {
        if (!proj.composer.enabled || rt_set_form(form)) return -1;
        arr_set(1, FORMS[form]);    /* the chosen form is saved with the musical project */
        proj_stale = 1;             /* the next Play also starts with the chosen form */
        return 0;
    }
    arr_set(1, FORMS[form]);
    proj_apply();
    return 0;
}

int proj_set_ambient(int on)             /* (arrangement ?? ArrangementConfig()), ambientMode on */
{
    if (rt_playing())
        return -1;
    arr_or_default();
    proj.arr_ambient = on ? 1 : 0;
    proj_apply();
    return 0;
}

/* the Mac has no live action for it: playing, the project's at once, the transport's at the next Play (1 while the
 * two differ) */
int proj_set_ratchet(int pct)
{
    if (pct < 0 || pct > 50 || pct % 5)
        return -1;
    proj.ratchet_pct = (uint8_t)pct;
    if (!rt_playing()) {
        proj_apply();
        return 0;
    }
    proj_stale = 1;
    return pct != proj_rt_ratchet;
}

void proj_set_channel(int voice, int ch)
{
    bl_template(proj.tpl, &proj_eng);
    if (voice < 0 || voice >= proj_eng.nvoices || ch < 0 || ch > 15)
        return;
    proj.channel[voice] = (uint8_t)ch;
    if (!rt_playing()) {
        proj_apply();
        return;
    }
    proj_stale = 1;
    rt_set_channel(bl_vid(proj_eng.voice[voice].id), (uint8_t)ch);   /* Transport.setChannel: at once */
}

int proj_get_delay(int voice, bl_delay_t *out)
{
    if (!out || rt_loading()) return -1;
    bl_template(proj.tpl, &proj_eng);
    if (voice < 0 || voice >= proj_eng.nvoices) return -1;
    if (rt_playing()) return rt_get_delay(bl_vid(proj_eng.voice[voice].id), out);
    *out = proj.delay[voice];
    return 0;
}

int proj_set_delay(int voice, const bl_delay_t *settings)
{
    if (!settings || !bl_delay_valid(*settings) || rt_loading()) return -1;
    bl_template(proj.tpl, &proj_eng);
    if (voice < 0 || voice >= proj_eng.nvoices) return -1;
    if (rt_playing() && rt_set_delay(bl_vid(proj_eng.voice[voice].id), settings)) return -1;
    proj.delay[voice] = *settings;
    proj_stale = 1;
    if (!rt_playing()) proj_apply();
    return 0;
}

int proj_set_delay_field(int voice, int field, int value)
{
    bl_delay_t settings;
    if (proj_get_delay(voice, &settings) || bl_delay_edit(&settings, field, value)) return -1;
    return proj_set_delay(voice, &settings);
}

int proj_set_sync(int ext)               /* existing control API; settings are global, despite the historic name */
{
    if (rt_playing())
        return -1;
    device_settings_t settings = device_settings_get();
    settings.sync = ext ? DEVICE_SYNC_EXTERNAL : DEVICE_SYNC_INTERNAL;
    return device_settings_set(settings);
}

void proj_set_ext_ports(int mask)
{
    rt_set_ext_port(mask);               /* compatibility entry point; no project storage */
}

int proj_dice(void)                      /* playing: LiveEngine(project).captureState() recalled */
{
    undo_push();
    proj.seed = proj_fresh_seed();
    proj_unseeded = 0;
    if (!rt_playing()) {
        proj_apply();
        return 0;
    }
    proj_to_engine(&proj_eng);
    bl_initial_state(&proj_eng, &proj_st);
    proj_stale = 1;
    return proj_recall(&proj_st);
}

/* ---- the slots and undo */
int slot_used(int k) { return k >= 0 && k < PROJ_SLOTS && slots[k].used; }

int slot_store(int k)
{
    static const uint32_t head[2] = {SLOT_MAGIC, SLOT_FORMAT};
    int n;
    if (k < 0 || k >= PROJ_SLOTS)
        return -1;
    rt_capture(&proj_st);
    if ((n = state_pack(&proj_st, proj_buf, STATE_MAX)) < 0)
        return -1;                       /* (a state the format cannot hold: the slot as it was) */
    memcpy(slots[k].rec, head, sizeof head);
    memcpy(slots[k].rec + SLOT_HEAD, proj_buf, (uint32_t)n);
    slots[k].n = (uint16_t)(n + SLOT_HEAD);
    slots[k].used = slots[k].dirty = 1;
    return 0;
}

static uint32_t undo_len(uint32_t at)   /* the length of the point at at */
{
    uint16_t n;
    memcpy(&n, undo_mem + at, sizeof n);
    return n;
}

static uint32_t undo_last(void)          /* where the latest point is: past the others */
{
    uint32_t at = 0;
    int k;
    for (k = 1; k < undo_n; k++)
        at += 2u + undo_len(at);
    return at;
}

/* a recall the engine's queue refused leaves no undo point for it (one the push dropped to make room stays gone) */
int slot_recall(int k)
{
    int pushed;
    if (!slot_used(k))
        return -1;
    pushed = !undo_push();
    uint32_t head[2];
    memcpy(head, slots[k].rec, sizeof head);
    if (state_unpack_format(slots[k].rec + SLOT_HEAD, slots[k].n - SLOT_HEAD, &proj_st, (int)head[1]) || proj_recall(&proj_st)) {
        if (pushed) {
            undo_used = (uint16_t)undo_last();
            undo_n--;
        }
        return -1;
    }
    return 0;
}

int undo_refused(void) { return undo_lost; }
int undo_depth(void) { return undo_n; }

int undo_push(void)
{
    uint16_t n16;
    uint32_t drop;
    int n;
    rt_capture(&proj_st);
    undo_lost = 1;
    if ((n = state_pack(&proj_st, proj_buf, STATE_MAX)) < 0)
        return -1;                       /* (a state the format cannot hold: no point) */
    undo_lost = 0;
    while (undo_n && (undo_n == PROJ_UNDO || undo_used + 2u + (uint32_t)n > UNDO_BYTES)) {   /* the oldest dropped */
        drop = 2u + undo_len(0);
        memmove(undo_mem, undo_mem + drop, undo_used - drop);
        undo_used = (uint16_t)(undo_used - drop);
        undo_n--;
    }
    n16 = (uint16_t)n;
    memcpy(undo_mem + undo_used, &n16, sizeof n16);
    memcpy(undo_mem + undo_used + 2, proj_buf, (uint32_t)n);
    undo_used = (uint16_t)(undo_used + 2u + (uint32_t)n);
    undo_n++;
    return 0;
}

int undo_pop(void)                      /* the point kept when the engine's queue refuses its recall */
{
    uint32_t at;
    if (!undo_n)
        return UNDO_NONE;
    at = undo_last();
    if (state_unpack(undo_mem + at + 2, (int)undo_len(at), &proj_st)) {
        undo_n--;                        /* (a point that does not unpack: gone, as if there were none) */
        undo_used = (uint16_t)at;
        return UNDO_NONE;
    }
    if (proj_recall(&proj_st))
        return UNDO_FULL;
    undo_n--;
    undo_used = (uint16_t)at;
    return 0;
}

#include "performance_presets.c"          /* musical records only; snapshots remain independent */
