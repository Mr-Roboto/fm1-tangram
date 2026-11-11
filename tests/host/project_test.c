/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The saved project on the host: app/project.c on stubs of the platform (the store a buffer per object, as fm1-base's
 * simulator's; the clock and the MASTER pot values the test sets) and of the transport (runtime.h: it keeps what it
 * is given, models materialized/pending state, and numbers captures in generation[0]).
 * The double records recall requests; it does not simulate a MIDI scheduler. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "app.h"
#include "state.h"
#include "state_same.h"

static uint8_t stub_obj[OBJ_NOBJ][PLAT_STORE_MAX];
static int stub_len[OBJ_NOBJ], stub_writes, stub_playing, stub_captures, stub_nact, stub_tempo, stub_chan[BL_NVIDS];
static int stub_busy;                    /* the transport still sending what its stop sends */
static uint32_t stub_ms = 1000, stub_master = 2048;
static bl_project_t stub_proj;
static bl_action_t stub_act;
static bl_state_t stub_recalled;
static bl_live_t stub_engine;
static bl_project_t stub_next;
static int stub_fresh = 1, stub_next_pending, stub_start_req, stub_action_error;
static int stub_sync, stub_ports = DEVICE_CLOCK_BOTH, stub_clock_bpm = 96, stub_clock_applies;
static int stub_midi_out = DEVICE_MIDI_BOTH, stub_clock_out = DEVICE_MIDI_BOTH;
static int stub_fail_save = -1, stub_bad_readback = -1, stub_logs;

static void stub_materialize(void)
{
    if (stub_fresh) {
        bl_live_init(&stub_engine, &stub_proj);
        stub_fresh = 0;
    }
}

static int stub_voice_index(int vid)
{
    if (vid < 0 || vid >= BL_NVIDS) return -1;
    for (int i = 0; i < stub_proj.nvoices; i++)
        if (bl_vid(stub_proj.voice[i].id) == vid) return i;
    return -1;
}

int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    if (obj >= OBJ_NOBJ || stub_len[obj] < 0) return -1;
    int n = stub_len[obj] < (int)max ? stub_len[obj] : (int)max;
    memcpy(dst, stub_obj[obj], (size_t)n);
    if ((int)obj == stub_bad_readback && n > 0) ((uint8_t *)dst)[n - 1] ^= 1u;
    return n;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    if (obj >= OBJ_NOBJ || len > PLAT_STORE_MAX) return -1;
    if ((int)obj == stub_fail_save) return -9;
    memcpy(stub_obj[obj], src, len);
    stub_len[obj] = (int)len;
    stub_writes++;
    return 0;
}
uint32_t plat_ms(void) { return stub_ms; }
uint32_t plat_master(void) { return stub_master; }
int rt_playing(void) { return stub_playing; }
int rt_idle(void) { return !stub_playing && !stub_busy; }
void rt_load(const bl_project_t *p)
{
    if (stub_playing) { stub_next = *p; stub_next_pending = 1; return; }
    stub_proj = *p;
    stub_fresh = 1;
    stub_next_pending = 0;
    for (int i = 0; i < p->nvoices; i++) {
        int vid = bl_vid(p->voice[i].id);
        if (vid >= 0 && vid < BL_NVIDS) stub_chan[vid] = p->voice[i].channel;
    }
    if (!stub_sync) stub_clock_bpm = p->bpm;
}
int rt_loading(void) { return stub_playing && stub_next_pending; }
const bl_live_t *rt_engine(void) { return &stub_engine; }
int rt_has_voice_state(void) { return !stub_fresh; }
int rt_get_voice_config(int vid, bl_vconfig_t *active, bl_vconfig_t *target)
{
    if (stub_voice_index(vid) < 0) return -1;
    stub_materialize();
    return bl_live_get_config(&stub_engine, vid, active, target);
}
int rt_set_voice_config(int vid, const bl_vconfig_t *target)
{
    int i = stub_voice_index(vid);
    if (i < 0 || !target || !bl_config_valid(target) || rt_loading()) return -1;
    if (stub_playing) {
        stub_materialize();
        if (bl_live_set_config(&stub_engine, vid, target)) return -1;
    }
    stub_proj.voice[i].has_config = 1;
    stub_proj.voice[i].config = *target;
    if (!stub_playing) stub_fresh = 1;
    return 0;
}
int rt_get_delay(int vid, bl_delay_t *out)
{
    int i = stub_voice_index(vid);
    if (i < 0 || !out) return -1;
    stub_materialize();
    *out = stub_engine.st[i].voice.delay;
    return 0;
}
int rt_set_delay(int vid, const bl_delay_t *settings)
{
    int i = stub_voice_index(vid);
    if (i < 0 || !settings || !bl_delay_valid(*settings) || rt_loading()) return -1;
    if (stub_playing) stub_materialize();
    stub_proj.voice[i].delay = *settings;
    if (stub_playing) {
        stub_engine.st[i].voice.delay = *settings;
        stub_engine.project.voice[i].delay = *settings;
    } else stub_fresh = 1;
    return 0;
}
int rt_action(const bl_action_t *a)
{
    if (!stub_playing) return 0;
    if (stub_action_error || (a->kind == BL_A_RECALL && !a->state)) return -1;
    if (a->kind == BL_A_KEY) {
        stub_materialize();
        if (bl_live_enqueue(&stub_engine, a, 0)) return -1;
    }
    stub_act = *a;
    stub_nact++;
    if (a->kind == BL_A_RECALL) stub_recalled = *a->state;
    return 0;
}
void rt_capture(bl_state_t *out)
{
    if (stub_fresh) bl_initial_state(&stub_proj, out);
    else bl_live_capture(&stub_engine, out);
    out->generation[0] = ++stub_captures;
}
void rt_set_tempo(int32_t bpm) { stub_tempo = bpm; }
void rt_set_channel(int vid, uint8_t ch)
{
    int i = stub_voice_index(vid);
    if (i < 0 || ch > 15) return;
    stub_chan[vid] = ch;
    stub_proj.voice[i].channel = ch;
    if (!stub_fresh) stub_engine.st[i].voice.midi_channel = ch;
}
int rt_apply_clock_settings(int ext, int ports)
{
    if ((ext != 0 && ext != 1) || !ports || (ports & ~DEVICE_CLOCK_ALL) ||
        (ext != stub_sync && (stub_playing || stub_start_req))) return -1;
    if (ext != stub_sync && !ext) stub_clock_bpm = stub_proj.bpm;
    stub_sync = ext;
    stub_ports = ports;
    stub_clock_applies++;
    return 0;
}
int rt_apply_output_settings(int midi_mask, int clock_mask)
{
    if (!midi_mask || (midi_mask & ~DEVICE_MIDI_BOTH) ||
        !clock_mask || (clock_mask & ~DEVICE_MIDI_BOTH) ||
        ((midi_mask != stub_midi_out || clock_mask != stub_clock_out) &&
          (stub_playing || stub_start_req || stub_busy))) return -1;
    stub_midi_out = midi_mask;
    stub_clock_out = clock_mask;
    return 0;
}
void rt_set_sync(int ext)
{
    device_settings_t settings = device_settings_get();
    settings.sync = ext ? DEVICE_SYNC_EXTERNAL : DEVICE_SYNC_INTERNAL;
    device_settings_set(settings);
}
void rt_set_ext_port(int mask)
{
    device_settings_t settings = device_settings_get();
    settings.ext_ports = (uint8_t)mask;
    device_settings_set(settings);
}
void plat_log(const char *line) { (void)line; stub_logs++; }

#include "../../app/device_settings.c"
#include "../../app/project.c"
static void host_saving(void) {}         /* (the simulator's look at the transport as a SAVE writes) */

static void store_put(uint32_t obj, const void *src, int n)
{
    memcpy(stub_obj[obj], src, (size_t)n);
    stub_len[obj] = n;
}

static void store_clear(void)
{
    for (int i = 0; i < OBJ_NOBJ; i++) stub_len[i] = -1;
    stub_playing = stub_busy = stub_next_pending = stub_start_req = stub_action_error = 0;
    stub_fresh = 1;
    stub_fail_save = stub_bad_readback = -1;
    stub_writes = stub_logs = stub_clock_applies = 0;
    bl_template(BL_TPL_FULL, &stub_proj);
    stub_sync = DEVICE_SYNC_INTERNAL;
    stub_ports = DEVICE_CLOCK_BOTH;
    stub_midi_out = stub_clock_out = DEVICE_MIDI_BOTH;
    stub_clock_bpm = stub_proj.bpm;
    device_settings_init();
    CHECK(!device_settings_apply(), "fixture initializes the real global settings module");
}

/* Independent byte-layout oracle. These are documented record offsets, not
 * sizeof/offsetof results or calls into the production codec. */
enum { FIXTURE_V6_BYTES = 301, FIXTURE_V5_BYTES = 336, FIXTURE_RECORD_CAP = 337 };
static void fixture_le(uint8_t *b, int at, uint64_t value, int width)
{
    for (int i = 0; i < width; i++) b[at + i] = (uint8_t)(value >> (8 * i));
}

static int fixture_record(const proj_t *p, int format, uint8_t *b)
{
    int old = format == 5;
    int size = old ? FIXTURE_V5_BYTES : FIXTURE_V6_BYTES;
    memset(b, old ? 0xA5 : 0, FIXTURE_RECORD_CAP); /* legacy alignment bytes need not be zero */
    fixture_le(b, 0, p->magic, 4);
    fixture_le(b, 4, (uint32_t)format, 4);
    b[8] = p->tpl; b[9] = p->key_root; b[10] = p->key_mode;
    if (old) { b[11] = 0; b[12] = DEVICE_CLOCK_TRS; } /* obsolete fields */
    b[old ? 13 : 11] = p->lights;
    fixture_le(b, old ? 16 : 12, p->seed, 8);
    fixture_le(b, old ? 24 : 20, (uint32_t)p->bpm, 4);
    int arr = old ? 28 : 24;
    b[arr] = p->arr_has; b[arr + 1] = p->arr_on; b[arr + 2] = p->arr_ambient; b[arr + 3] = p->arr_reprise;
    fixture_le(b, old ? 32 : 28, (uint16_t)p->arr_min, 2);
    fixture_le(b, old ? 34 : 30, (uint16_t)p->arr_max, 2);
    b[old ? 36 : 32] = p->density_pct; b[old ? 37 : 33] = p->has_density;
    b[old ? 45 : 41] = p->ratchet_pct;
    for (int i = 0; i < 7; i++) {
        b[(old ? 38 : 34) + i] = p->channel[i];
        int d = (old ? 46 : 42) + 4 * i;
        b[d] = p->delay[i].enabled; b[d + 1] = p->delay[i].time;
        b[d + 2] = p->delay[i].feedback; b[d + 3] = p->delay[i].decay;
        b[(old ? 74 : 70) + i] = p->voice_configured[i];
        const bl_vconfig_t *c = &p->voice_config[i];
        int q = (old ? 84 : 77) + i * (old ? 36 : 32);
        fixture_le(b, q, (uint32_t)c->key.root, 4);
        b[q + 4] = c->key.scale.mode; b[q + 5] = c->key.scale.count;
        for (int j = 0; j < 12; j++) b[q + 6 + j] = (uint8_t)c->key.scale.off[j];
        b[q + (old ? 20 : 18)] = c->gen.kind;
        fixture_le(b, q + (old ? 22 : 19), (uint16_t)c->gen.a, 2);
        fixture_le(b, q + (old ? 24 : 21), (uint16_t)c->gen.b, 2);
        b[q + (old ? 26 : 23)] = c->gen.direction;
        b[q + (old ? 27 : 24)] = c->gen.groove;
        fixture_le(b, q + (old ? 28 : 25), c->length, 4);
        b[q + (old ? 32 : 29)] = c->reg_low;
        b[q + (old ? 33 : 30)] = c->reg_high;
        b[q + (old ? 34 : 31)] = c->phase;
    }
    return size;
}

static void store_project(const proj_t *p)
{
    uint8_t record[FIXTURE_RECORD_CAP];
    int n = fixture_record(p, 6, record);
    store_put(OBJ_PROJ, record, n);
}

static int project_equal(const proj_t *a, const proj_t *b)
{
    if (a->magic != b->magic || a->format != b->format || a->tpl != b->tpl || a->key_root != b->key_root ||
        a->key_mode != b->key_mode || a->lights != b->lights || a->seed != b->seed || a->bpm != b->bpm ||
        a->arr_has != b->arr_has || a->arr_on != b->arr_on || a->arr_ambient != b->arr_ambient ||
        a->arr_reprise != b->arr_reprise || a->arr_min != b->arr_min || a->arr_max != b->arr_max ||
        a->density_pct != b->density_pct || a->has_density != b->has_density || a->ratchet_pct != b->ratchet_pct)
        return 0;
    for (int i = 0; i < BL_MAX_VOICES; i++) {
        if (a->channel[i] != b->channel[i] || a->voice_configured[i] != b->voice_configured[i] ||
            bl_config_diff(&a->voice_config[i], &b->voice_config[i]) ||
            a->delay[i].enabled != b->delay[i].enabled || a->delay[i].time != b->delay[i].time ||
            a->delay[i].feedback != b->delay[i].feedback || a->delay[i].decay != b->delay[i].decay) return 0;
    }
    return 1;
}

static int arr_is(const bl_arrcfg_t *a, int enabled, const bl_arrcfg_t *preset)
{
    return a->enabled == enabled && a->ambient == preset->ambient && a->reprise == preset->reprise &&
           a->min_bars == preset->min_bars && a->max_bars == preset->max_bars;
}

/* proj as proj_defaults makes it (the Demo trio in A minor, its seed still to draw), apart from the seed */
static int proj_is_defaults(void)
{
    proj_t got = proj;
    int unseeded = proj_unseeded;
    proj_defaults();
    proj_t def = proj;
    def.seed = got.seed;
    proj = got;
    proj_unseeded = (uint8_t)unseeded;
    return project_equal(&got, &def) && got.tpl == BL_TPL_DEMO && got.key_root == 9 &&
           got.key_mode == BL_MODE_NATURAL_MINOR && !got.arr_has;
}

/* a record that is not the project's (none, another magic, another format, short, a value out of its range) loads
 * as the defaults and says so, their seed drawn fresh at the first Play; the project's own loads as it was */
static void test_store_bad_magic(void)
{
    proj_t good, rec;
    store_clear();
    CHECK(proj_load() != 0 && proj_is_defaults(), "nothing saved");
    proj_defaults();
    good = proj;
    good.tpl = BL_TPL_FULL;
    good.key_root = 2;
    good.key_mode = BL_MODE_DORIAN;
    good.bpm = 120;
    good.seed = UINT64_C(0x123456789ABCDEF0);
    good.arr_has = 1;
    good.arr_on = 0;
    good.arr_reprise = 1;
    good.arr_min = 12;
    good.arr_max = 24;
    good.has_density = 1;
    good.density_pct = 35;
    good.channel[2] = 11;
    good.lights = 2;

    rec = good;
    rec.magic = 0x314F4E45u;                            /* AMB-1's "ENO1" */
    store_project(&rec);
    CHECK(proj_load() != 0 && proj_is_defaults(), "another magic");
    stub_ms = 2000;
    proj_ready();
    uint64_t seed = proj.seed;
    CHECK(!proj_unseeded && stub_proj.seed == seed, "the first Play draws the seed and plays it");
    CHECK(proj_load() != 0 && proj_unseeded, "the defaults again");
    stub_ms = 3000;
    proj_ready();
    CHECK(proj.seed != seed, "a fresh seed each time: %llx", (unsigned long long)proj.seed);
    for (uint32_t f = 0; f < 3; f++) {
        rec = good;
        rec.format = f ? PROJ_FORMAT + f : 0;
        uint8_t record[FIXTURE_RECORD_CAP];
        int n = fixture_record(&rec, 6, record);
        fixture_le(record, 4, rec.format, 4);
        store_put(OBJ_PROJ, record, n);
        CHECK(proj_load() != 0 && proj_is_defaults(), "format %u", rec.format);
    }
    static const int SHORT[] = {0, 4, 8, FIXTURE_V6_BYTES - 1};
    uint8_t record[FIXTURE_RECORD_CAP];
    fixture_record(&good, 6, record);
    for (int i = 0; i < 4; i++) {
        store_put(OBJ_PROJ, record, SHORT[i]);
        CHECK(proj_load() != 0 && proj_is_defaults(), "a record of %d bytes", SHORT[i]);
    }
    for (int i = 0; i < 17; i++) {
        rec = good;
        switch (i) {
        case 0: rec.tpl = BL_NTPL; break;
        case 1: rec.key_root = 12; break;
        case 2: rec.key_mode = BL_NMODES; break;
        case 3: rec.bpm = 39; break;
        case 4: rec.bpm = 221; break;
        case 5: rec.arr_on = 2; break;
        case 6: rec.density_pct = 101; break;
        case 7: rec.has_density = 2; break;
        case 8: rec.channel[6] = 16; break;
        case 9: rec.voice_configured[0] = 2; break;
        case 10: rec.delay[0].feedback = 0; break;
        case 11: rec.lights = 3; break;
        case 12: rec.arr_min = 0; break;
        case 13: rec.arr_min = 25; break;
        case 14: rec.arr_has = 0; break;                /* nil with a config */
        case 15: rec.arr_ambient = 2; break;
        case 16: rec.arr_has = 2; break;
        }
        store_project(&rec);
        CHECK(proj_load() != 0 && proj_is_defaults(), "value %d out of range", i);
    }
    store_put(OBJ_PROJ, record, FIXTURE_V6_BYTES + 1);
    CHECK(proj_load() != 0 && proj_is_defaults(), "trailing project byte rejected");
    store_project(&good);
    CHECK(proj_load() == 0 && project_equal(&proj, &good) && !proj_unseeded, "the project's own record");
}

/* Format 6 is compact; format 5 is decoded from its documented historical
 * byte layout, including poisoned alignment and ignored clock fields. */
static void test_store_formats(void)
{
    proj_t good, rec;
    uint8_t record[FIXTURE_RECORD_CAP], expected[FIXTURE_RECORD_CAP];
    CHECK(PROJ_FORMAT == 6 && BL_MAX_VOICES == 7 && SLOT_FORMAT == 8,
          "documented format 6 / seven voices / slot format 8");
    store_clear();
    proj_load();
    CHECK(proj.ratchet_pct == 0, "the defaults: off");
    proj_defaults();
    good = proj;
    good.tpl = BL_TPL_AMBIENT;
    good.key_root = 4;
    good.key_mode = BL_MODE_LYDIAN;
    good.bpm = 77;
    good.seed = UINT64_C(0xFEDCBA9876543210);
    good.arr_has = good.arr_on = good.arr_ambient = 1;
    good.arr_min = 8;
    good.arr_max = 16;
    good.has_density = 1;
    good.density_pct = 60;
    good.channel[4] = 9;
    good.lights = 0;
    good.voice_configured[0] = 1;
    bl_vconfig_t *c = &good.voice_config[0];
    c->key.root = 2;
    bl_scale_preset(BL_MODE_HIRA, &c->key.scale);
    c->gen = (bl_gencfg_t){BL_GEN_MOTIF,240,73,BL_DIR_M_PENDULUM,BL_GROOVE_BROKEN_8};
    c->length = 3600; c->reg_low = 48; c->reg_high = 84; c->phase = 3;
    good.delay[0] = (bl_delay_t){1,7,100,50};
    good.ratchet_pct = 35;
    device_settings_t clock = {DEVICE_SYNC_EXTERNAL,DEVICE_CLOCK_USB,DEVICE_THEME_CLASSIC,1,
        DEVICE_MIDI_BOTH,DEVICE_MIDI_BOTH};
    CHECK(!device_settings_set(clock), "independent global clock setup");
    stub_clock_bpm = 125;
    int applies = stub_clock_applies;
    for (int variant = 0; variant < 2; variant++) {
        int n = fixture_record(&good, 5, record);
        if (variant) { record[11] = 255; record[12] = 255; }
        store_put(OBJ_PROJ, record, n);
        CHECK(proj_load() == 0 && project_equal(&proj, &good) && !proj_unseeded,
              "format 5 preserves musical data; obsolete clock variant %d ignored",variant);
        proj_apply();
        CHECK(device_settings_get().sync == DEVICE_SYNC_EXTERNAL && device_settings_get().ext_ports == DEVICE_CLOCK_USB &&
              stub_sync == DEVICE_SYNC_EXTERNAL && stub_ports == DEVICE_CLOCK_USB && stub_clock_bpm == 125 &&
              stub_clock_applies == applies, "format 5 load/apply cannot apply old clock bytes");
        CHECK(proj_save() == 0, "format 5 upgrades only on SAVE");
        int expected_n = fixture_record(&good, 6, expected);
        CHECK(expected_n == 301 && stub_len[OBJ_PROJ] == 301 && !memcmp(stub_obj[OBJ_PROJ],expected,301),
              "format 6 write matches independent 301-byte expectation, including voice config");
        memset(&proj,0,sizeof proj);
        CHECK(!proj_load() && project_equal(&proj,&good),"format 6 reload restores every musical field");
    }
    fixture_record(&good,5,record);
    const int legacy_lengths[] = {FIXTURE_V5_BYTES - 1, FIXTURE_V5_BYTES + 1};
    for (unsigned i=0;i<sizeof legacy_lengths/sizeof legacy_lengths[0];i++) {
        store_put(OBJ_PROJ,record,legacy_lengths[i]);
        CHECK(proj_load()!=0 && proj_is_defaults(),"format 5 length %d rejected",legacy_lengths[i]);
    }
    fixture_record(&good,6,record);
    fixture_le(record,4,1,4);
    store_put(OBJ_PROJ,record,FIXTURE_V6_BYTES);
    CHECK(proj_load()!=0 && proj_is_defaults(),"format 1 is unsupported, never migrated");
    static const uint8_t BAD[] = {7, 55, 60, 255};
    for (int i = 0; i < 4; i++) {
        rec = good;
        rec.ratchet_pct = BAD[i];
        store_project(&rec);
        CHECK(proj_load() != 0 && proj_is_defaults(), "ratchet %d", BAD[i]);
    }
}

/* the ratchet to the engine as Legitimizer.Config(ratchetProbability: Double(pct) / 100), never nil; stopped, the
 * transport at once; playing, the project's only, 1 (the next Play) while it differs from the transport's */
static void test_ratchet(void)
{
    bl_project_t got;
    store_clear();
    proj_load();
    stub_playing = 0;
    proj_to_engine(&got);
    CHECK(got.has_ratchet && f64_eq(got.ratchet_p, F64(0)), "off: 0, not nil");
    CHECK(proj_set_ratchet(25) == 0 && proj.ratchet_pct == 25 && stub_proj.has_ratchet &&
          f64_eq(stub_proj.ratchet_p, f64_div(f64_from_i64(25), F64(0x4059000000000000))), "25: 25.0 / 100");
    CHECK(proj_set_ratchet(-5) == -1 && proj_set_ratchet(55) == -1 && proj_set_ratchet(12) == -1 &&
          proj.ratchet_pct == 25, "out of range");
    CHECK(proj_set_template(BL_TPL_DEMO) == 0 && proj.ratchet_pct == 25 && f64_eq(stub_proj.ratchet_p,
          f64_div(f64_from_i64(25), F64(0x4059000000000000))), "a template keeps it");
    stub_playing = 1;
    int nact = stub_nact;
    CHECK(proj_set_ratchet(50) == 1 && proj.ratchet_pct == 50 && proj_stale && stub_nact == nact &&
          f64_eq(stub_proj.ratchet_p, f64_div(f64_from_i64(25), F64(0x4059000000000000))),
          "playing: the project's, no action, the transport's at the next Play");
    CHECK(proj_set_ratchet(25) == 0, "playing, back to the transport's: nothing waits for it");
    CHECK(proj_set_ratchet(0) == 1, "and away again");
    stub_playing = 0;
    CHECK(proj_stopped() == 1 && !proj_stale && f64_eq(stub_proj.ratchet_p, F64(0)), "the stop: the transport's");
    proj_set_ratchet(0);
}

/* the defaults' seed: the template's own until the first Play draws one from the clock and the pot (once); a
 * template loaded or Dice draws its own and the Play then keeps it */
static void test_default_seed(void)
{
    bl_project_t demo;
    bl_rng_t r;
    bl_template(BL_TPL_DEMO, &demo);
    store_clear();
    proj_load();
    proj_apply();
    CHECK(proj_unseeded && proj.seed == demo.seed, "before the first Play: %llx", (unsigned long long)proj.seed);
    stub_ms = 5000;
    proj_ready();
    bl_rng_seed(&r, (uint64_t)5000 << 32 | stub_master);
    uint64_t want = bl_rng_next(&r);
    CHECK(proj.seed == want && stub_proj.seed == want, "drawn at the Play: %llx", (unsigned long long)proj.seed);
    stub_ms = 6000;
    proj_ready();
    CHECK(proj.seed == want, "once");
    proj_load();
    CHECK(proj_set_template(BL_TPL_FULL) == 0 && !proj_unseeded, "a template draws its own");
    want = proj.seed;
    stub_ms = 7000;
    proj_ready();
    CHECK(proj.seed == want, "the Play keeps the template's");
}

/* the app project as Berlinan's: the template, then its seed, key, tempo, arrangement, density and channels. The
 * arrangement is the Mac's: a template's config as it is, AUTO off keeping the form, AUTO or ambient mode on a nil
 * one from ArrangementConfig(), a form its preset */
static void test_proj_to_engine(void)
{
    bl_project_t got, want;
    static const bl_arrcfg_t DEFAULT = {0, 0, 0, 8, 16};
    stub_playing = 0;
    proj_defaults();
    proj.tpl = BL_TPL_FULL;
    proj.seed = 42;
    proj.key_root = 2;
    proj.key_mode = BL_MODE_DORIAN;
    proj.bpm = 133;
    proj.has_density = 1;
    proj.density_pct = 35;
    proj.channel[1] = 7;
    proj_to_engine(&got);
    bl_template(BL_TPL_FULL, &want);
    bl_scale_preset(BL_MODE_DORIAN, &want.key.scale);
    CHECK(got.seed == 42 && got.key.root == 2 && !memcmp(&got.key.scale, &want.key.scale, sizeof want.key.scale) &&
          got.bpm == 133, "seed, key, tempo");
    CHECK(f64_eq(got.density, f64_div(f64_from_i64(35), F64(0x4059000000000000))), "density 35 / 100");
    CHECK(got.voice[1].channel == 7 && got.voice[0].channel == want.voice[0].channel &&
          got.nvoices == want.nvoices, "channels");
    CHECK(!got.has_arr && proj_form() == FORM_OTHER, "the template's nil");
    proj.has_density = 0;
    proj_to_engine(&got);
    CHECK(f64_eq(got.density, want.density), "the template's density");

    CHECK(proj_set_arranger(1) == 0 && stub_proj.has_arr && arr_is(&stub_proj.arr, 1, &DEFAULT) &&
          proj_form() == FORM_OTHER, "AUTO on from nil: ArrangementConfig(), enabled");
    CHECK(proj_set_form(FORM_CLASSIC) == 0 && arr_is(&stub_proj.arr, 1, &bl_arr_classic) &&
          proj_form() == FORM_CLASSIC, "classic");
    CHECK(proj_set_arranger(0) == 0 && arr_is(&stub_proj.arr, 0, &bl_arr_classic) && proj_form() == FORM_CLASSIC,
          "AUTO off keeps the form");
    CHECK(proj_set_arranger(1) == 0 && arr_is(&stub_proj.arr, 1, &bl_arr_classic), "and on again");
    bl_arrcfg_t amb = bl_arr_classic;
    amb.ambient = 1;
    CHECK(proj_set_ambient(1) == 0 && arr_is(&stub_proj.arr, 1, &amb) && proj_form() == FORM_OTHER,
          "ambient mode on a classic arc");
    CHECK(proj_set_template(BL_TPL_DEMO) == 0 && !proj.arr_has && proj_set_ambient(1) == 0 &&
          arr_is(&stub_proj.arr, 0, &(bl_arrcfg_t){0, 1, 0, 8, 16}), "ambient mode on nil: ArrangementConfig()");

    CHECK(proj_set_template(BL_TPL_AMBIENT) == 0 && proj.bpm == 72 && proj.key_root == 9 &&
          proj.key_mode == BL_MODE_NATURAL_MINOR && proj.channel[0] == 0 && proj.channel[3] == 3 &&
          !proj.has_density && proj.seed != 42, "a template's own values, a fresh seed");
    CHECK(arr_is(&stub_proj.arr, 1, &bl_arr_ambient) && proj_form() == FORM_AMBIENT, "its plateau, on");
    CHECK(proj_set_arranger(0) == 0 && arr_is(&stub_proj.arr, 0, &bl_arr_ambient), "off: its plateau kept");
    CHECK(proj_set_form(4) == -1 && proj_set_form(FORM_OTHER) == -1, "no such form");
}

/* a change while playing goes into the project at once, and what the Mac does live to the transport; the transport
 * gets the project when it stops; what the Mac allows stopped only is refused while playing; SAVE while playing
 * waits for the stop; Dice */
static void test_proj_stale(void)
{
    store_clear();
    proj_load();
    proj_set_template(BL_TPL_FULL);
    stub_playing = 0;
    proj_apply();
    stub_playing = 1;
    proj_set_bpm(120);
    CHECK(proj.bpm == 120 && stub_tempo == 120 && stub_proj.bpm == 96, "tempo: the project's, the transport's from "
          "the bar, the transport's project the old one");
    CHECK(proj_set_density(50) == 0 && stub_act.kind == BL_A_DENSITY && proj.has_density && proj.density_pct == 50,
          "density live");
    CHECK(proj_set_key(4, BL_MODE_PHRYGIAN) == 0 && stub_act.kind == BL_A_KEY && stub_act.key.root == 4,
          "key live");
    CHECK(proj_set_arranger(1) == 0 && stub_act.kind == BL_A_ARRANGER && stub_act.arg == 1 && proj.arr_on,
          "AUTO on live");
    CHECK(proj_set_form(FORM_SLOW) == -1 && proj_set_ambient(1) == -1 && proj_form() == FORM_OTHER &&
          !proj.arr_ambient, "a form, ambient mode while playing");
    CHECK(proj_set_sync(1) == -1 && device_settings_get().sync == DEVICE_SYNC_INTERNAL, "sync while playing");
    int nact = stub_nact;
    proj_set_bpm(120);
    CHECK(stub_nact == nact, "the same tempo does nothing");
    CHECK(stub_proj.bpm == 96 && f64_eq(stub_proj.density, F64(0x3FF0000000000000)), "nothing rebuilt yet");

    stub_writes = 0;                                    /* SAVE while playing: at the stop */
    CHECK(proj_save() == 1 && proj_save_pending() && stub_writes == 0, "SAVE while playing waits");
    CHECK(proj_stopped() == 1 && stub_writes == 0 && stub_proj.bpm == 96, "playing: no rebuild, no write");
    stub_playing = 0;
    stub_busy = 1;                                      /* the stop's Stop and offs still going out: not yet */
    CHECK(proj_stopped() == 1 && stub_writes == 0 && proj_save_pending() && stub_proj.bpm == 120,
          "the stop: the transport rebuilt, the SAVE waiting for the wire (%d)", stub_writes);
    stub_busy = 0;
    CHECK(proj_stopped() == 0 && stub_writes == 1 && !proj_save_pending(), "then written (%d)", stub_writes);
    CHECK(stub_proj.bpm == 120 && f64_eq(stub_proj.density, F64(0x3FE0000000000000)) && stub_proj.key.root == 4 &&
          stub_proj.has_arr && stub_proj.arr.enabled, "stopped: the project to the transport");
    stub_proj.bpm = 1;
    CHECK(proj_stopped() == 1 && stub_proj.bpm == 1 && stub_writes == 1, "once");
    CHECK(proj_save() == 0 && stub_writes == 2, "SAVE stopped: at once");
    CHECK(proj_set_form(FORM_SLOW) == 0 && stub_proj.arr.min_bars == 12 && proj_set_sync(1) == 0 &&
          device_settings_get().sync == DEVICE_SYNC_EXTERNAL,
          "stopped: a form and sync");
    CHECK(!proj_set_sync(0),"restore global INTERNAL through its setter");

    stub_playing = 1;                                   /* Dice: an undo point, the fresh state recalled */
    uint64_t seed = proj.seed;
    stub_ms += 7;
    int caps = stub_captures;
    CHECK(proj_dice() == 0 && proj.seed != seed && stub_captures == caps + 1 && stub_act.kind == BL_A_RECALL,
          "dice");
    bl_project_t p;
    static bl_state_t want;
    proj_to_engine(&p);
    bl_initial_state(&p, &want);
    CHECK(state_same(&stub_recalled, &want), "dice recalls LiveEngine(project).captureState()");
    stub_playing = 0;
}

/* the slots: SAVE writes the project and each slot stored since, read back; the load takes each slot as it was,
 * a slot record that is not one stays empty; recall pushes an undo point, an empty slot does nothing */
static void test_store_slots(void)
{
    static bl_state_t want;
    store_clear();
    proj_load();
    proj_apply();
    for (int k = 0; k < PROJ_SLOTS; k++) CHECK(!slot_used(k), "slot %d used", k);
    CHECK(slot_store(-1) == -1 && slot_store(PROJ_SLOTS) == -1 && !slot_used(PROJ_SLOTS), "no such slot");
    CHECK(slot_store(2) == 0 && slot_used(2), "store slot 2");
    bl_initial_state(&stub_proj, &want);
    want.generation[0] = stub_captures;
    stub_writes = 0;
    CHECK(proj_save() == 0 && stub_writes == 2 && !proj_unseeded, "SAVE: %d writes, the seed drawn", stub_writes);
    CHECK(stub_len[OBJ_SLOT2] > 8 && stub_obj[OBJ_SLOT2][4] == 8 && stub_obj[OBJ_SLOT2][5] == 0 &&
          stub_obj[OBJ_SLOT2][6] == 0 && stub_obj[OBJ_SLOT2][7] == 0,"saved slot wrapper is still format 8");
    CHECK(proj_save() == 0 && stub_writes == 3, "SAVE again: the project alone (%d)", stub_writes);
    store_put(OBJ_SLOT4, "garbage", 7);
    uint8_t rec[64];
    memcpy(rec, stub_obj[OBJ_SLOT2], sizeof rec);
    store_put(OBJ_SLOT3, rec, sizeof rec);              /* the header, the state cut short */
    proj.bpm = 1;
    CHECK(proj_load() == 0 && proj.bpm == 90, "load");
    for (int k = 0; k < PROJ_SLOTS; k++) CHECK(slot_used(k) == (k == 2), "slot %d after the load", k);
    stub_playing = 1;
    int caps = stub_captures, nact = stub_nact;
    CHECK(slot_recall(1) == -1 && stub_captures == caps && stub_nact == nact, "an empty slot: nothing");
    CHECK(slot_recall(2) == 0 && stub_captures == caps + 1 && state_same(&stub_recalled, &want), "recall slot 2");
    CHECK(undo_pop() == 0 && stub_recalled.generation[0] == caps + 1, "its undo point");
    CHECK(undo_pop() == -1, "no more");
    stub_playing = 0;
}

/* undo: 20 deep, the oldest dropped, the latest first; a load empties it */
static void test_undo_depth(void)
{
    store_clear();
    proj_load();
    proj_apply();
    stub_playing = 1;
    int first = stub_captures + 1;
    for (int i = 0; i < 25; i++) undo_push();
    int ok = 0;
    for (int i = 0; i < PROJ_UNDO; i++) ok += undo_pop() == 0 && stub_recalled.generation[0] == first + 24 - i;
    CHECK(ok == PROJ_UNDO && undo_pop() == -1, "%d of 20 undone in order", ok);
    undo_push();
    proj_load();
    CHECK(undo_pop() == -1, "a load empties the undo stack");
    stub_playing = 0;
}

static void test_runtime_interfaces(void)
{
    static bl_note_t notes[128];
    static bl_state_t captured, restored;
    uint8_t snapshot[PLAT_STORE_MAX];
    store_clear(); proj_load(); proj_apply();
    CHECK(!rt_has_voice_state(),"loaded project has not been materialized");
    bl_vconfig_t active = {0}, pending = {0};
    CHECK(rt_get_voice_config(BL_VID_NONE,&active,&pending)==-1 &&
          rt_set_voice_config(BL_VID_NONE,&pending)==-1 && rt_get_delay(BL_VID_NONE,NULL)==-1,
          "runtime double rejects absent voices");
    CHECK(!rt_get_voice_config(BL_VID_BED1,&active,&pending) && rt_has_voice_state() &&
          !bl_config_diff(&active,&pending),"getter materializes real initial active/pending frames");
    bl_vconfig_t original = active, target = active;
    target.key.root=2; bl_scale_preset(BL_MODE_DORIAN,&target.key.scale);
    target.gen=(bl_gencfg_t){BL_GEN_ARP,240,73,BL_DIR_DOWN,BL_GROOVE_PULSE_8};
    target.length=3600; target.reg_low=48; target.reg_high=84; target.phase=3;
    stub_playing=1;
    CHECK(!proj_set_voice_config(0,&target) && !rt_get_voice_config(BL_VID_BED1,&active,&pending) &&
          !bl_config_diff(&active,&original) && !bl_config_diff(&pending,&target),
          "project setter stages actual runtime pending configuration");
    bl_vconfig_t invalid=target; invalid.gen.kind=BL_NGENS;
    CHECK(rt_set_voice_config(BL_VID_BED1,&invalid)==-1 && rt_set_voice_config(BL_VID_BED1,NULL)==-1,
          "invalid config cannot be accepted by the double");
    bl_live_render(&stub_engine,0,3839,notes,128);
    CHECK(!bl_config_diff(&stub_engine.st[0].voice.config,&original),"active frame unchanged before local boundary");
    bl_live_render(&stub_engine,3839,3840,notes,128);
    CHECK(!bl_config_diff(&stub_engine.st[0].voice.config,&target) &&
          !stub_engine.st[0].voice.config_dirty && stub_engine.st[0].voice.pat.length==3600,
          "real engine applies complete pending frame after the old loop finishes");
    CHECK(!proj_set_voice_config_field(0,BL_CFG_ROOT,7),"stage a subsequent root edit");
    target.key.root=7;
    bl_delay_t delay={1,5,50,80}, got;
    CHECK(!proj_set_delay(0,&delay) && !rt_get_delay(BL_VID_BED1,&got) &&
          !memcmp(&delay,&got,sizeof got),"delay state follows accepted project edits");
    bl_delay_t bad_delay=delay; bad_delay.feedback=0;
    CHECK(rt_set_delay(BL_VID_BED1,&bad_delay)==-1 && rt_set_delay(BL_VID_NONE,&delay)==-1 &&
          rt_set_delay(BL_VID_BED1,NULL)==-1,"invalid delay settings and absent voices rejected");
    rt_capture(&captured);
    int n=state_pack(&captured,snapshot,sizeof snapshot);
    CHECK(n>0 && !state_unpack(snapshot,n,&restored) && state_same(&captured,&restored) &&
          !bl_config_diff(&captured.v[0].config,&restored.v[0].config) &&
          !bl_config_diff(&captured.v[0].pending,&restored.v[0].pending) &&
          captured.v[0].config_dirty==restored.v[0].config_dirty &&
          !memcmp(&captured.v[0].delay,&restored.v[0].delay,sizeof delay),
          "snapshot retains distinct active/pending frames and delay");
    CHECK(captured.v[0].config.key.root==2 && captured.v[0].pending.key.root==7,
          "capture does not prematurely activate a pending edit");
    stub_playing=0;
    CHECK(proj_stopped()==1 && !proj_stale && !bl_config_diff(&proj.voice_config[0],&target),
          "stop reconciles genuine pending targets into the musical project");
    CHECK(!proj_save() && !proj_load(),"configured project saves and reloads");
    proj_apply();
    CHECK(!rt_get_voice_config(BL_VID_BED1,&active,&pending) && !bl_config_diff(&active,&target) &&
          !bl_config_diff(&pending,&target) && !rt_get_delay(BL_VID_BED1,&got) &&
          !memcmp(&delay,&got,sizeof delay),"restored project materializes saved target and delay");

    stub_playing=1;
    bl_project_t next; bl_template(BL_TPL_FULL,&next);
    rt_load(&next);
    CHECK(rt_loading() && !rt_get_voice_config(BL_VID_BED1,&active,&pending) &&
          !bl_config_diff(&active,&target),"pending load leaves current materialized engine in place");
    CHECK(proj_get_voice_config(0,&active)==-1 && proj_set_voice_config(0,&target)==-1 &&
          proj_get_delay(0,&got)==-1 && proj_set_delay(0,&delay)==-1 &&
          rt_set_voice_config(BL_VID_BED1,&target)==-1 && rt_set_delay(BL_VID_BED1,&delay)==-1,
          "project and runtime edits refuse while a replacement is loading");
    /* Explicit fixture takeover: timing itself is covered by live/runtime tests. */
    stub_proj=stub_next; stub_next_pending=0; stub_fresh=1;
    CHECK(!rt_loading() && !rt_get_voice_config(BL_VID_BED1,&active,&pending) && active.key.root==9 &&
          active.gen.kind==BL_GEN_SEED_PROCEDURE,"completed load exposes replacement defaults");
    stub_playing=0;
}

static void test_storage_failures(void)
{
    uint8_t saved[FIXTURE_RECORD_CAP];
    static uint8_t saved_slot[PLAT_STORE_MAX];
    store_clear(); proj_load(); proj_apply(); proj_ready();
    CHECK(!proj_save(),"valid baseline saved before failure injection");
    memcpy(saved,stub_obj[OBJ_PROJ],FIXTURE_V6_BYTES);
    proj_set_bpm(137);
    int writes=stub_writes;
    stub_fail_save=OBJ_PROJ;
    CHECK(proj_save()==-1 && stub_writes==writes && !memcmp(saved,stub_obj[OBJ_PROJ],FIXTURE_V6_BYTES),
          "failed project write reports failure and retains previous record");
    stub_fail_save=-1; stub_bad_readback=OBJ_PROJ;
    CHECK(proj_save()==-1,"mismatching project readback cannot report success");
    stub_bad_readback=-1;
    CHECK(!proj_save() && !proj_load() && proj.bpm==137,"project write recovers without changing expectations");
    proj_apply();
    CHECK(!slot_store(2),"slot baseline captured");
    stub_fail_save=OBJ_SLOT2;
    CHECK(proj_save()==-1 && slots[2].dirty && slot_used(2),"failed slot write retains dirty state for retry");
    stub_fail_save=-1;
    CHECK(!proj_save() && !slots[2].dirty,"successful retry clears slot dirty state");
    int slot_bytes=slots[2].n;
    memcpy(saved_slot,slots[2].rec,(size_t)slot_bytes);
    bl_vconfig_t config;
    CHECK(!rt_get_voice_config(BL_VID_BED1,&config,NULL),"materialize snapshot failure source");
    uint8_t velocity=stub_engine.st[0].voice.pat.note[0].velocity;
    stub_engine.st[0].voice.pat.note[0].velocity=0;
    CHECK(slot_store(2)==-1 && slot_used(2) && slots[2].n==slot_bytes &&
          !memcmp(saved_slot,slots[2].rec,(size_t)slot_bytes),
          "invalid captured material cannot replace an existing slot");
    stub_engine.st[0].voice.pat.note[0].velocity=velocity;
    stub_playing=1;
    CHECK(!undo_push() && undo_depth()==1,"undo failure baseline");
    stub_action_error=1;
    CHECK(undo_pop()==UNDO_FULL && undo_depth()==1,"refused recall preserves undo point");
    stub_action_error=0;
    CHECK(!undo_pop() && !undo_depth(),"accepted recall consumes the preserved point");
    stub_playing=0;
}

static void test_global_settings(void)
{
    store_clear(); proj_load(); proj_apply();
    CHECK(device_settings_get().sync==DEVICE_SYNC_INTERNAL && device_settings_get().ext_ports==DEVICE_CLOCK_BOTH,
          "missing global record uses documented defaults");
    device_settings_t settings={DEVICE_SYNC_EXTERNAL,DEVICE_CLOCK_TRS,DEVICE_THEME_CLASSIC,1,
        DEVICE_MIDI_BOTH,DEVICE_MIDI_BOTH};
    CHECK(!device_settings_set(settings) && device_settings_status()==1,"global edit pending");
    stub_clock_bpm=125;
    int applies=stub_clock_applies;
    CHECK(!proj_save() && stub_len[OBJ_DEVICE_SETTINGS]==-1 && stub_sync==DEVICE_SYNC_EXTERNAL &&
          stub_ports==DEVICE_CLOCK_TRS && stub_clock_bpm==125 && stub_clock_applies==applies,
          "project SAVE neither persists nor reapplies global clock settings");
    CHECK(device_settings_poll()==1,"global edits settle before persistence");
    stub_ms+=300; stub_busy=1;
    CHECK(device_settings_poll()==1 && stub_len[OBJ_DEVICE_SETTINGS]==-1,"pending MIDI releases defer global write");
    stub_busy=0; stub_playing=1;
    CHECK(device_settings_poll()==1 && stub_len[OBJ_DEVICE_SETTINGS]==-1,"playing defers global write");
    stub_playing=0;
    static const uint8_t expected[]={ 'T','G','D','S',4,DEVICE_SYNC_EXTERNAL,DEVICE_CLOCK_TRS,
        DEVICE_THEME_CLASSIC,1,DEVICE_MIDI_BOTH,DEVICE_MIDI_BOTH };
    CHECK(!device_settings_poll() && stub_len[OBJ_DEVICE_SETTINGS]==11 &&
          !memcmp(stub_obj[OBJ_DEVICE_SETTINGS],expected,sizeof expected),"real module writes independent eleven-byte record");
    stub_sync=DEVICE_SYNC_INTERNAL; stub_ports=DEVICE_CLOCK_USB; stub_clock_bpm=96;
    CHECK(!device_settings_init() && !device_settings_apply() && stub_sync==DEVICE_SYNC_EXTERNAL &&
          stub_ports==DEVICE_CLOCK_TRS,"reboot reloads persisted global settings instead of retaining RAM");
    applies=stub_clock_applies;
    settings.sync=2;
    CHECK(device_settings_set(settings)==-1,"invalid global sync rejected");
    settings.sync=DEVICE_SYNC_EXTERNAL; settings.ext_ports=1u<<PORT_BLE;
    CHECK(device_settings_set(settings)==-1 && stub_clock_applies==applies,"unsupported global input rejected before runtime");
    settings.ext_ports=DEVICE_CLOCK_TRS; settings.sync=DEVICE_SYNC_INTERNAL;
    stub_playing=1;
    CHECK(device_settings_set(settings)==-1 && device_settings_get().sync==DEVICE_SYNC_EXTERNAL,
          "playing refuses global source changes");
    stub_playing=0; stub_start_req=1;
    CHECK(device_settings_set(settings)==-1,"pending Start refuses mode change");
    stub_start_req=0;
    settings.sync=DEVICE_SYNC_EXTERNAL; settings.ext_ports=DEVICE_CLOCK_USB;
    CHECK(!device_settings_set(settings),"valid routing edit");
    stub_ms+=300; stub_fail_save=OBJ_DEVICE_SETTINGS;
    int writes=stub_writes, logs=stub_logs;
    CHECK(device_settings_poll()==-9 && device_settings_status()==-9 && stub_writes==writes && stub_logs==logs+1,
          "global write failure remains pending and is logged");
    CHECK(device_settings_poll()==-9 && stub_logs==logs+1,"global failure does not retry automatically");
    stub_fail_save=-1; device_settings_retry(); stub_ms+=300;
    CHECK(!device_settings_poll() && !device_settings_init() && device_settings_get().ext_ports==DEVICE_CLOCK_USB,
          "explicit retry persists the retained edit");
    writes=stub_writes;
    settings.ext_ports=DEVICE_CLOCK_BOTH;
    CHECK(!device_settings_set(settings),"coalesced edit");
    settings.ext_ports=DEVICE_CLOCK_USB;
    CHECK(!device_settings_set(settings) && !device_settings_status(),"return to persisted settings cancels write");
    stub_ms+=300;
    CHECK(!device_settings_poll() && stub_writes==writes,"unchanged globals never rewrite flash");
}

/* MIDI/clock destination selection is independent of external MIDI-clock input.
 * Preserve older on-device records; switching outputs is refused until idle. */
static void test_ble_global_routing(void)
{
    store_clear();
    device_settings_t s = device_settings_get();
    CHECK(s.midi_out == DEVICE_MIDI_BOTH && s.clock_out == DEVICE_MIDI_BOTH,
          "BLE rollout defaults both output streams to USB plus BLE");
    s.ext_ports = DEVICE_CLOCK_ALL;
    s.midi_out = DEVICE_MIDI_BLE;
    s.clock_out = DEVICE_MIDI_USB;
    CHECK(!device_settings_set(s) && stub_ports == DEVICE_CLOCK_ALL &&
          stub_midi_out == DEVICE_MIDI_BLE && stub_clock_out == DEVICE_MIDI_USB,
          "input USB+TRS+BLE and separate outputs are applied");
    stub_ms += 300;
    CHECK(!device_settings_poll() && stub_len[OBJ_DEVICE_SETTINGS] == 11 &&
          stub_obj[OBJ_DEVICE_SETTINGS][4] == 4 &&
          stub_obj[OBJ_DEVICE_SETTINGS][6] == DEVICE_CLOCK_ALL &&
          stub_obj[OBJ_DEVICE_SETTINGS][9] == DEVICE_MIDI_BLE &&
          stub_obj[OBJ_DEVICE_SETTINGS][10] == DEVICE_MIDI_USB,
          "format 4 persists external input and independent destinations");
    stub_midi_out = stub_clock_out = DEVICE_MIDI_BOTH;
    CHECK(!device_settings_init() && !device_settings_apply() &&
          stub_midi_out == DEVICE_MIDI_BLE && stub_clock_out == DEVICE_MIDI_USB &&
          stub_ports == DEVICE_CLOCK_ALL,
          "reboot restores both persisted output destinations and BLE input");
    s = device_settings_get();
    s.clock_out = DEVICE_MIDI_BLE;
    stub_playing = 1;
    CHECK(device_settings_set(s) == -1 && stub_clock_out == DEVICE_MIDI_USB,
          "output routing cannot change while playing");
    stub_playing = 0;
    stub_busy = 1;
    CHECK(device_settings_set(s) == -1 && stub_clock_out == DEVICE_MIDI_USB,
          "output routing cannot change until Note-Offs have drained");
    stub_busy = 0;
    CHECK(!device_settings_set(s) && stub_clock_out == DEVICE_MIDI_BLE,
          "output routing can change after drain");
    s.midi_out = 0;
    CHECK(device_settings_set(s) == -1, "no output target is rejected");

    /* Prior BLE firmware used 9-byte format 3. Keep theme, lights, and
     * external clock source; initialize the two new output choices safely. */
    static const uint8_t legacy3[] = {
        'T','G','D','S',3,DEVICE_SYNC_INTERNAL,DEVICE_CLOCK_TRS,DEVICE_THEME_AMBER,2
    };
    store_put(OBJ_DEVICE_SETTINGS, legacy3, sizeof legacy3);
    CHECK(!device_settings_init() && !device_settings_apply() &&
          device_settings_get().theme == DEVICE_THEME_AMBER &&
          device_settings_get().lights == 2 &&
          device_settings_get().ext_ports == DEVICE_CLOCK_TRS &&
          device_settings_get().midi_out == DEVICE_MIDI_BOTH &&
          device_settings_get().clock_out == DEVICE_MIDI_BOTH,
          "format 3 migrates without losing other globals or BLE defaults");
}

int main(void)
{
    test_store_bad_magic();
    test_store_formats();
    test_ratchet();
    test_default_seed();
    test_proj_to_engine();
    test_proj_stale();
    test_store_slots();
    test_undo_depth();
    test_runtime_interfaces();
    test_storage_failures();
    test_global_settings();
    test_ble_global_routing();
    printf(fails ? "project: %d FAILED\n" : "project: ok\n", fails);
    return fails != 0;
}
