/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The saved project on the host: app/project.c on stubs of the platform (the store a buffer per object, as fm1-base's
 * simulator's; the clock and the MASTER pot values the test sets) and of the transport (runtime.h: it keeps what it
 * is given, and captures its project's initial state with generation[0] counting the captures). */
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

int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    if (obj >= OBJ_NOBJ || stub_len[obj] < 0) return -1;
    int n = stub_len[obj] < (int)max ? stub_len[obj] : (int)max;
    memcpy(dst, stub_obj[obj], (size_t)n);
    return n;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    if (obj >= OBJ_NOBJ || len > PLAT_STORE_MAX) return -1;
    memcpy(stub_obj[obj], src, len);
    stub_len[obj] = (int)len;
    stub_writes++;
    return 0;
}
uint32_t plat_ms(void) { return stub_ms; }
uint32_t plat_master(void) { return stub_master; }
int rt_playing(void) { return stub_playing; }
int rt_idle(void) { return !stub_playing && !stub_busy; }
void rt_load(const bl_project_t *p) { stub_proj = *p; }
int rt_action(const bl_action_t *a)
{
    stub_act = *a;
    stub_nact++;
    if (a->kind == BL_A_RECALL) stub_recalled = *a->state;
    return 0;
}
void rt_capture(bl_state_t *out)
{
    bl_initial_state(&stub_proj, out);
    out->generation[0] = ++stub_captures;
}
void rt_set_tempo(int32_t bpm) { stub_tempo = bpm; }
void rt_set_channel(int vid, uint8_t ch) { stub_chan[vid] = ch; }
void rt_set_sync(int ext) { (void)ext; }
void rt_set_ext_port(int mask) { (void)mask; }

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
    return !memcmp(&got, &def, sizeof def) && got.tpl == BL_TPL_DEMO && got.key_root == 9 &&
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
    good.sync = 1;
    good.ext_ports = 1u << PORT_TRS;
    good.lights = 2;

    rec = good;
    rec.magic = 0x314F4E45u;                            /* AMB-1's "ENO1" */
    store_put(OBJ_PROJ, &rec, sizeof rec);
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
        store_put(OBJ_PROJ, &rec, sizeof rec);
        CHECK(proj_load() != 0 && proj_is_defaults(), "format %u", rec.format);
    }
    static const int SHORT[] = {0, 4, 8, (int)sizeof(proj_t) - 1};
    for (int i = 0; i < 4; i++) {
        store_put(OBJ_PROJ, &good, SHORT[i]);
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
        case 9: rec.sync = 2; break;
        case 10: rec.ext_ports = 1u << PORT_BLE; break;
        case 11: rec.lights = 3; break;
        case 12: rec.arr_min = 0; break;
        case 13: rec.arr_min = 25; break;
        case 14: rec.arr_has = 0; break;                /* nil with a config */
        case 15: rec.arr_ambient = 2; break;
        case 16: rec.arr_has = 2; break;
        }
        store_put(OBJ_PROJ, &rec, sizeof rec);
        CHECK(proj_load() != 0 && proj_is_defaults(), "value %d out of range", i);
    }
    store_put(OBJ_PROJ, &good, sizeof good);
    CHECK(proj_load() == 0 && !memcmp(&proj, &good, sizeof good) && !proj_unseeded, "the project's own record");
}

/* format 2, the ratchet: off in the defaults; a format-1 record (the same 48 bytes, the ratchet's byte its padding,
 * any value) loads with everything kept and the ratchet off; in format 2 a ratchet that is not 0-50 in steps of 5
 * is a record out of range */
static void test_store_format1(void)
{
    proj_t good, rec;
    CHECK(PROJ_FORMAT == 2 && sizeof(proj_t) == 48, "format %u, %d bytes as format 1's", PROJ_FORMAT,
          (int)sizeof(proj_t));
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
    rec = good;
    rec.format = 1;
    rec.ratchet_pct = 0xA5;                             /* format 1's padding: whatever it held */
    store_put(OBJ_PROJ, &rec, sizeof rec);
    good.ratchet_pct = 0;
    CHECK(proj_load() == 0 && !memcmp(&proj, &good, sizeof good) && !proj_unseeded, "format 1: kept, the ratchet off");
    good.ratchet_pct = 35;
    store_put(OBJ_PROJ, &good, sizeof good);
    CHECK(proj_load() == 0 && proj.ratchet_pct == 35, "format 2: its ratchet");
    static const uint8_t BAD[] = {7, 55, 60, 255};
    for (int i = 0; i < 4; i++) {
        rec = good;
        rec.ratchet_pct = BAD[i];
        store_put(OBJ_PROJ, &rec, sizeof rec);
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
    CHECK(proj_set_sync(1) == -1 && proj.sync == 0, "sync while playing");
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
    CHECK(proj_set_form(FORM_SLOW) == 0 && stub_proj.arr.min_bars == 12 && proj_set_sync(1) == 0 && proj.sync == 1,
          "stopped: a form and sync");
    proj.sync = 0;

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

int main(void)
{
    test_store_bad_magic();
    test_store_format1();
    test_ratchet();
    test_default_seed();
    test_proj_to_engine();
    test_proj_stale();
    test_store_slots();
    test_undo_depth();
    printf(fails ? "project: %d FAILED\n" : "project: ok\n", fails);
    return fails != 0;
}
