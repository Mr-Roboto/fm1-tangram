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
 *   the stop (transportStale, rebuildTransportIfStale). The arranger's form and ambient mode and the sync are
 *   stopped only.
 * - The arrangement is the Mac's: nil or a config, with AUTO (enabled), the form (a preset's periods and reprise) and
 *   ambient mode in it apart; AUTO or ambient mode on a nil one starts from ArrangementConfig(); off keeps the rest.
 * - A slot stores the transport's state at the render head (captureState: before a Play of the project in place,
 *   its initial state). Recall and undo enqueue a recall, which a stopped transport drops (Play starts with no
 *   actions). Undo: a point before every destructive action, 20 deep, the oldest dropped; a load empties it
 *   (loadSlots).
 * Where it is the FM-1's own:
 * - SAVE while playing waits for the stop (proj_stopped: the button's, MIDI Stop, the arranger's ending): a flash
 *   write stops everything for a moment, the MIDI clock too.
 * - A stale project reaches the transport after any stop, not only the PLAY button's (the Mac's rebuild).
 * - A channel is the template's voice i's (the UI's voice i).
 * - The slots and the undo points are kept packed (a bl_state_t is 16.8 KB), the undo points in 32 KB: 20 of the
 *   templates' states fit with room to spare, a larger state takes the room of more than one. */
#include "state.h"

#define SLOT_MAGIC 0x31534C42u           /* "BLS1" */
#define SLOT_FORMAT 1u
#define SLOT_HEAD 8                      /* magic, format */
#define STATE_MAX ((int)PLAT_STORE_MAX - SLOT_HEAD)
#define PORTS_ALL (1u << PORT_USB | 1u << PORT_TRS)
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
static bl_state_t proj_st PLAT_POOL;     /* a state captured, unpacked */
static bl_project_t proj_eng PLAT_POOL;  /* the project as the engine's, a template */
static uint8_t proj_buf[PLAT_STORE_MAX] PLAT_POOL;   /* a state packed; a record read back */

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
    memset(proj.channel, 0, sizeof proj.channel);
    for (i = 0; i < proj_eng.nvoices; i++)
        proj.channel[i] = proj_eng.voice[i].channel;
    proj.seed = seed;
}

void proj_defaults(void)
{
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    proj.ext_ports = PORTS_ALL;
    proj.lights = 1;
    from_template(BL_TPL_DEMO, 0);
    proj.seed = proj_eng.seed;           /* (the template's own until the first Play draws one) */
    proj_unseeded = 1;
}

static int proj_valid(const proj_t *p)
{
    int i;
    if (p->magic != PROJ_MAGIC || p->format != PROJ_FORMAT)
        return 0;
    if (p->tpl >= BL_NTPL || p->key_root > 11 || p->key_mode >= BL_NMODES || p->sync > 1 || (p->ext_ports & ~PORTS_ALL)
        || p->lights > 2 || p->bpm < 40 || p->bpm > 220 || p->density_pct > 100 || p->has_density > 1)
        return 0;
    if (p->arr_has > 1 || p->arr_on > 1 || p->arr_ambient > 1 || p->arr_reprise > 1)
        return 0;
    if (p->arr_has ? p->arr_min < 1 || p->arr_min > p->arr_max || p->arr_max > 1024
                   : p->arr_on || p->arr_ambient || p->arr_reprise || p->arr_min || p->arr_max)
        return 0;
    for (i = 0; i < BL_MAX_VOICES; i++)
        if (p->channel[i] > 15)
            return 0;
    return 1;
}

static void slot_load(int k)             /* the slot's record, if it is one (its state unpacks) */
{
    uint32_t head[2];
    int n = plat_store_load(OBJ_SLOT0 + (uint32_t)k, slots[k].rec, PLAT_STORE_MAX);
    slots[k].used = slots[k].dirty = 0;
    slots[k].n = 0;
    if (n <= SLOT_HEAD)
        return;
    memcpy(head, slots[k].rec, sizeof head);
    if (head[0] != SLOT_MAGIC || head[1] != SLOT_FORMAT
        || state_unpack(slots[k].rec + SLOT_HEAD, n - SLOT_HEAD, &proj_st))
        return;
    slots[k].used = 1;
    slots[k].n = (uint16_t)n;
}

int proj_load(void)
{
    static proj_t rec;
    int n, k;
    undo_used = undo_n = 0;
    proj_stale = proj_saving = 0;
    for (k = 0; k < PROJ_SLOTS; k++)
        slot_load(k);
    memset(&rec, 0, sizeof rec);
    n = plat_store_load(OBJ_PROJ, &rec, sizeof rec);
    if (n == (int)sizeof rec && proj_valid(&rec)) {
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

int proj_save(void)                      /* the project; the slots stored since (the others are on the flash) */
{
    int k, r;
    if (rt_playing()) {
        proj_saving = 1;
        return 1;
    }
    proj_saving = 0;
    draw_seed();                         /* (a project saved has its seed) */
    r = save_obj(OBJ_PROJ, &proj, sizeof proj);
    for (k = 0; k < PROJ_SLOTS; k++)
        if (slots[k].dirty) {
            if (save_obj(OBJ_SLOT0 + (uint32_t)k, slots[k].rec, slots[k].n))
                r = -1;
            else
                slots[k].dirty = 0;
        }
    return r;
}

static f64 density_of(int pct) { return f64_div(f64_from_i64(pct), HUNDRED); }   /* Double(pct) / 100 */

void proj_to_engine(bl_project_t *out)   /* the template in A minor, then the project's own values */
{
    int i;
    bl_template(proj.tpl, out);
    out->seed = proj.seed;
    out->key.root = proj.key_root;
    bl_scale_preset(proj.key_mode, &out->key.scale);
    out->bpm = proj.bpm;
    out->has_arr = proj.arr_has;
    arr_get(&out->arr);
    if (proj.has_density)
        out->density = density_of(proj.density_pct);
    for (i = 0; i < out->nvoices; i++)
        out->voice[i].channel = proj.channel[i];
}

void proj_apply(void)                    /* rebuildTransport: a stopped transport takes the project */
{
    if (rt_playing()) {
        proj_stale = 1;
        return;
    }
    proj_to_engine(&proj_eng);
    rt_set_sync(proj.sync);
    rt_set_ext_port(proj.ext_ports);
    rt_load(&proj_eng);
    proj_stale = 0;
}

void proj_ready(void)
{
    draw_seed();
    if (proj_stale)
        proj_apply();
}

int proj_stopped(void)
{
    int r = 1;
    if (rt_playing())
        return 1;
    if (proj_saving)
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
    proj_stale = 1;
    return 0;
}

int proj_set_template(int tpl) { return template_seeded(tpl, proj_fresh_seed()); }

int proj_set_key(int root, int mode)
{
    bl_action_t a;
    if (root < 0 || root > 11 || mode < 0 || mode >= BL_NMODES)
        return -1;
    if (!rt_playing()) {
        proj.key_root = (uint8_t)root;
        proj.key_mode = (uint8_t)mode;
        proj_apply();
        return 0;
    }
    if (root == proj.key_root && mode == proj.key_mode)   /* (the key it has: no change) */
        return 0;
    proj.key_root = (uint8_t)root;
    proj.key_mode = (uint8_t)mode;
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
    a.value = density_of(pct);
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

int proj_set_form(int form)              /* arrangement = the preset (enabled, as the presets are) */
{
    if (form < FORM_CLASSIC || form > FORM_AMBIENT || rt_playing())
        return -1;
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

int proj_set_sync(int ext)
{
    if (rt_playing())
        return -1;
    proj.sync = ext ? 1 : 0;
    rt_set_sync(proj.sync);
    return 0;
}

void proj_set_ext_ports(int mask)
{
    proj.ext_ports = (uint8_t)(mask & PORTS_ALL);
    rt_set_ext_port(proj.ext_ports);
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

int slot_recall(int k)
{
    if (!slot_used(k))
        return -1;
    undo_push();
    if (state_unpack(slots[k].rec + SLOT_HEAD, slots[k].n - SLOT_HEAD, &proj_st))
        return -1;
    return proj_recall(&proj_st);
}

static uint32_t undo_len(uint32_t at)   /* the length of the point at at */
{
    uint16_t n;
    memcpy(&n, undo_mem + at, sizeof n);
    return n;
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

int undo_pop(void)
{
    uint32_t at = 0;
    int k;
    if (!undo_n)
        return -1;
    for (k = 1; k < undo_n; k++)         /* the latest: past the others */
        at += 2u + undo_len(at);
    undo_n--;
    undo_used = (uint16_t)at;
    if (state_unpack(undo_mem + at + 2, (int)undo_len(at), &proj_st))
        return -1;
    return proj_recall(&proj_st);
}
