/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The three templates (project.h), as Project.swift defines them and TemplateStore.inDefaultKey puts them: the key
 * A natural minor, nothing else changed. tests/equiv/templates.ref is what the Mac makes of them. */
#include <string.h>
#include "project.h"
#include "legit.h"

const char *const BL_VID_NAME[BL_NVIDS] = {"bedPluck1", "bedPluck2", "bass", "pad", "lead", "kick", "drone"};
const char *const BL_SCN_NAME[BL_NSCNS] = {"closed", "open"};
const char *const BL_TPL_NAME[BL_NTPL] = {"demo", "full", "ambient"};

int bl_vid(const char *id)
{
    for (int i = 0; i < BL_NVIDS; i++)
        if (!strcmp(BL_VID_NAME[i], id)) return i;
    return BL_VID_NONE;
}

int bl_scn(const char *name)
{
    for (int i = 0; i < BL_NSCNS; i++)
        if (!strcmp(BL_SCN_NAME[i], name)) return i;
    return BL_SCN_NONE;
}

#define ONE F64(0x3FF0000000000000)    /* 1.0 */

/* a voice with no lanes and no CC map */
static bl_vcfg_t voice(const char *id, uint8_t role, uint8_t low, uint8_t high, int32_t ref_c, uint8_t channel,
                       uint8_t kind, int16_t a, int16_t b)
{
    bl_vcfg_t v;
    memset(&v, 0, sizeof v);
    v.id = id;
    v.role = role;
    v.reg_low = low;
    v.reg_high = high;
    v.ref_c = ref_c;
    v.channel = channel;
    v.anchor = BL_ANCHOR_NONE;
    v.gen = (bl_gencfg_t){kind, a, b};
    return v;
}

/* ccMap: ["filter": 74] */
static void filter_map(bl_vcfg_t *v)
{
    v->ncc = 1;
    v->cc_name[0] = "filter";
    v->cc_num[0] = 74;
}

/* CCLane(target: "filter", shape: .sine, cycleBars:, depth:, center:), phase 0 */
static void filter_lane(bl_vcfg_t *v, f64 cycle_bars, f64 depth, f64 center)
{
    v->lane[v->nlanes++] = (bl_cclane_t){BL_LFO_SINE, 0, cycle_bars, depth, center, F64(0x0000000000000000)};
}

static void common(bl_project_t *p, uint64_t seed, int32_t bpm)
{
    memset(p, 0, sizeof *p);
    p->seed = seed;
    p->bpm = bpm;
    p->key.root = 9;                                   /* TemplateStore.defaultKey: A natural minor */
    bl_scale_preset(BL_MODE_NATURAL_MINOR, &p->key.scale);
    p->density = ONE;
    for (int i = 0; i < 7; i++) p->scene_by_phase[i] = -1;
}

/* Project.demo: two phasing bed plucks (16 against 15 steps), a pulsing bass, the walk i-VI-iv-i-v-i at 90 BPM */
static void demo(bl_project_t *p)
{
    static const bl_tev_t walk[] = {{8, 5}, {12, 3}, {16, 0}, {24, 4}, {28, 0}};
    common(p, UINT64_C(0xBE1271A9), 90);
    p->voice[0] = voice("bedPluck1", BL_ROLE_BED1, 55, 79, 60, 0, BL_GEN_SEED_PROCEDURE, 1, 4);
    filter_lane(&p->voice[0], F64(0x4030000000000000), F64(0x3FD3333333333333), F64(0x3FE3333333333333));
    filter_map(&p->voice[0]);                          /* 16 bars, depth 0.3, center 0.6 */
    p->voice[1] = voice("bedPluck2", BL_ROLE_BED2, 60, 84, 60, 1, BL_GEN_SEED_PROCEDURE, 1, 4);
    p->voice[1].phase_steps = -1;
    p->voice[2] = voice("bass", BL_ROLE_BASS, 24, 48, 36, 2, BL_GEN_PULSING_BASS, 3, 0);
    p->voice[2].anchor = BL_ANCHOR_BEATS13;
    p->nvoices = 3;
    bl_sched_merge(&p->transposition, walk, 5, 0);
}

/* Project.fullEnsemble: the demo at 96 BPM with its own seed, the bass a bass-melody walk, and a chord-layer pad, a
 * lead and the heartbeat kick */
static void full(bl_project_t *p)
{
    demo(p);
    p->seed = UINT64_C(2477861893);
    p->bpm = 96;
    p->voice[2] = voice("bass", BL_ROLE_BASS, 24, 48, 36, 2, BL_GEN_BASS_MELODY, 16, 0);
    p->voice[3] = voice("pad", BL_ROLE_PAD, 48, 72, 60, 3, BL_GEN_CHORD_LAYER, 7, 0);
    filter_lane(&p->voice[3], F64(0x4040000000000000), F64(0x3FD999999999999A), F64(0x3FE0000000000000));
    filter_map(&p->voice[3]);                          /* 32 bars, depth 0.4, center 0.5 */
    p->voice[4] = voice("lead", BL_ROLE_LEAD, 60, 96, 72, 4, BL_GEN_STAGED_PLUCK, 2, 0);
    p->voice[5] = voice("kick", BL_ROLE_RHYTHM, 36, 47, 36, 9, BL_GEN_HEARTBEAT, 0, 0);
    p->nvoices = 6;
}

/* a scene over the ambient's four voices (drone, bedPluck1, bedPluck2, pad), each on its filter */
static void scene4(bl_scene_t *s, const char *name, f64 drone, f64 bed1, f64 bed2, f64 pad)
{
    const f64 value[4] = {drone, bed1, bed2, pad};
    s->name = name;
    s->n = 4;
    for (int i = 0; i < 4; i++) {
        s->voice[i] = (uint8_t)i;
        s->target[i] = 0;
        s->value[i] = value[i];
    }
}

/* Project.ambientDrift: a drone-led, ambient-arranged setup at 72 BPM over the pedal, its timbre journey (the scenes
 * closed and open) mapped to the arranger's phases */
static void ambient(bl_project_t *p)
{
    common(p, UINT64_C(0x0A3B11ED), 72);
    p->voice[0] = voice("drone", BL_ROLE_DRONE, 36, 60, 48, 0, BL_GEN_DRONE, 8, 0);
    p->voice[1] = voice("bedPluck1", BL_ROLE_BED1, 55, 79, 60, 1, BL_GEN_SEED_PROCEDURE, 1, 3);
    p->voice[2] = voice("bedPluck2", BL_ROLE_BED2, 60, 84, 60, 2, BL_GEN_SEED_PROCEDURE, 1, 3);
    p->voice[2].phase_steps = -1;
    p->voice[3] = voice("pad", BL_ROLE_PAD, 48, 72, 60, 3, BL_GEN_CHORD_LAYER, 7, 0);
    p->nvoices = 4;
    for (int i = 0; i < 4; i++) filter_map(&p->voice[i]);
    p->has_arr = 1;
    p->arr = bl_arr_ambient;
    /* closed: 0.25, 0.3, 0.3, 0.2; open: 0.6, 0.75, 0.75, 0.65 */
    scene4(&p->scene[0], "closed", F64(0x3FD0000000000000), F64(0x3FD3333333333333), F64(0x3FD3333333333333),
           F64(0x3FC999999999999A));
    scene4(&p->scene[1], "open", F64(0x3FE3333333333333), F64(0x3FE8000000000000), F64(0x3FE8000000000000),
           F64(0x3FE4CCCCCCCCCCCD));
    p->nscenes = 2;
    p->scene_by_phase[BL_PH_BED] = 0;
    p->scene_by_phase[BL_PH_PAYOFF] = 1;
    p->scene_by_phase[BL_PH_SUBTRACT] = 0;
}

void bl_template(int which, bl_project_t *out)
{
    switch (which) {
    case BL_TPL_FULL: full(out); break;
    case BL_TPL_AMBIENT: ambient(out); break;
    default: demo(out); break;
    }
}
