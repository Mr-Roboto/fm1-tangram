/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The script language of tests/equiv/README.md on the C engine: the parser, the run (a transport as RefTool's Run
 * plays it: the actions as the app performs them, the wake, the windows, the ending) and the reference lines it
 * writes (N, C, S, H; T in place of H for the state text). Host code with no test harness in it, so that the app's
 * host build (app/host.c) can include it as tests/host/equiv_test.c does. */
#pragma once
#include <stdint.h>
#include "live.h"
#include "project.h"

/* the script's actions */
enum {
    BL_SA_TRANSPOSE, BL_SA_REGENERATE, BL_SA_REROLL, BL_SA_BLANK, BL_SA_LOCK, BL_SA_SAVE, BL_SA_RECALL, BL_SA_KEY,
    BL_SA_DENSITY, BL_SA_BPART, BL_SA_VOICE, BL_SA_TENSION, BL_SA_SCENE, BL_SA_ARRANGER, BL_SA_ADVANCE, BL_SA_RESEED,
    BL_SA_LOAD
};

/* `at TICK ACTION`: voice a BL_VID_; a the degree offset, slot, root, K, 0/1 or N; b the mode; tpl and seed of a
 * load or reseed */
typedef struct {
    bl_tick_t tick;
    uint8_t kind, voice, tpl;
    int32_t a, b;
    uint64_t seed;
} bl_sact_t;

/* a parsed script: arranger -1 none, else 0 off, 1 classic, 2 slow, 3 ambient; density -1 none */
typedef struct {
    int tpl, has_seed, arranger, density;
    uint64_t seed;
    bl_tick_t window;
    int32_t bars;
    int nact, cap;
    bl_sact_t *act;
} bl_script_t;

/* parse text; 0, or -1 with the reason in err ("line 3: ...") */
int bl_script_parse(const char *text, bl_script_t *s, char *err, int errmax);
void bl_script_free(bl_script_t *s);

/* the project the app holds before play: the template in A minor, the seed, the arranger, the density */
void bl_script_project(const bl_script_t *s, bl_project_t *out);

/* one play of a script; emit gets each line (without "\n") and returns nonzero to stop the run. text: T lines (the
 * state text) in place of each H line. Returns 0, or -1 with the reason in err when the script goes beyond the C
 * engine's capacities (a queue full, a window of more notes or CC than the run holds), which the Mac's has not */
typedef int (*bl_emit_fn)(void *ctx, const char *line);
int bl_script_play(const bl_script_t *s, int text, bl_emit_fn emit, void *ctx, char *err, int errmax);
