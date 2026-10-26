/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The engine's side of a project as Berlinan has it (Project.swift): the voices with their generators, registers and
 * CC lanes, the tempo, key and transposition, the arranger's config, the density and the CC scenes; and the three
 * templates as the Mac instantiates them (TemplateStore.inDefaultKey: A natural minor, each with its own seed). The
 * fields the templates leave nil read as the engine reads nil: density 1, the default legitimization (4 repeats,
 * ratchet 0.15), the default velocity policy and B part, a tension budget of 2, the slides off. Not ported, as no
 * template has them: a meter other than 4/4, harmony modes, alternating phrases, gate ramps, accumulators, override
 * patterns, MIDI delays, a voice left out of the transposition. */
#pragma once
#include <stdint.h>
#include "bl.h"
#include "f64.h"
#include "music.h"
#include "gen.h"
#include "harmony.h"
#include "arrange.h"
#include "cc.h"

/* The voice ids and scene names of the templates. An action names a voice or a scene by one of these codes rather
 * than by its place in a project, because the app's project and the running engine's may differ (a template loaded
 * while the old one plays to its bar line) and Swift's actions carry the id string. BL_VID_NONE / BL_SCN_NONE: an id
 * that is none of them */
enum { BL_VID_BED1, BL_VID_BED2, BL_VID_BASS, BL_VID_PAD, BL_VID_LEAD, BL_VID_KICK, BL_VID_DRONE, BL_NVIDS };
enum { BL_SCN_CLOSED, BL_SCN_OPEN, BL_NSCNS };
#define BL_VID_NONE 255
#define BL_SCN_NONE 255
extern const char *const BL_VID_NAME[BL_NVIDS];    /* "bedPluck1" ... "drone" */
extern const char *const BL_SCN_NAME[BL_NSCNS];    /* "closed", "open" */
int bl_vid(const char *id);                        /* the code of an id, BL_VID_NONE when none */
int bl_scn(const char *name);                      /* the code of a scene name, BL_SCN_NONE when none */

/* VoiceConfig: role a BL_ROLE_, register reg_low...reg_high, anchor a BL_ANCHOR_ (none for nil), phase_steps 0 for
 * nil; the CC lanes, each lane's target the index of its name in the CC map cc_name/cc_num (an index of ncc or more:
 * an unmapped lane, which renders nothing) */
typedef struct {
    const char *id;
    uint8_t role, reg_low, reg_high, channel, anchor, nlanes;
    int32_t ref_c;
    int8_t phase_steps;
    bl_gencfg_t gen;
    bl_cclane_t lane[BL_MAX_LANES];
    uint8_t ncc;
    const char *cc_name[2];
    uint8_t cc_num[2];
} bl_vcfg_t;

/* CCScene: per entry a voice (its index in the project), a target (the index of its name in that voice's CC map)
 * and the destination 0-1 */
typedef struct {
    const char *name;
    uint8_t n;
    uint8_t voice[BL_MAX_VOICES];
    uint8_t target[BL_MAX_VOICES];
    f64 value[BL_MAX_VOICES];
} bl_scene_t;

/* Project: has_arr 0 is arrangement nil; density nil is 1; scene_by_phase[phase] the index of the scene the
 * arranger's phase fires (sceneByPhase), -1 for none */
typedef struct {
    uint64_t seed;
    int32_t bpm;
    bl_key_t key;
    uint8_t nvoices, has_arr, nscenes;
    bl_vcfg_t voice[BL_MAX_VOICES];
    bl_sched_t transposition;
    bl_arrcfg_t arr;
    f64 density;
    int8_t scene_by_phase[7];
    bl_scene_t scene[BL_MAX_SCENES];
} bl_project_t;

/* the templates: Project.demo, .fullEnsemble, .ambientDrift */
enum { BL_TPL_DEMO, BL_TPL_FULL, BL_TPL_AMBIENT, BL_NTPL };
extern const char *const BL_TPL_NAME[BL_NTPL];     /* "demo", "full", "ambient" */
/* the template in A natural minor with its own seed (0xBE1271A9, 2477861893, 0x0A3B11ED) */
void bl_template(int which, bl_project_t *out);
