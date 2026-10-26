/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan's live engine (LiveEngine.swift, with the parts of OfflineRenderer.swift it calls): the pull-model
 * engine the transport asks for consecutive tick windows. It renders every active voice's loops against the shared
 * transposition, counts their plays, mutates and regenerates them at their loop boundaries, binds the bar-quantized
 * actions where they land (a window splits there), runs the arranger once a bar, and renders the CC lanes and the
 * scene morphs. Same project, same actions at the same ticks and the same windows give the Mac's notes and CC, field
 * for field and in the same order (tests/equiv/README.md).
 *
 * Swift's queues have no bound; these hold BL_MAX_PENDING bar-bound changes (BL_MAX_RECALLS of them recalls, each
 * a copy of its state), BL_MAX_ENABLES arranger on/offs and BL_MAX_ADVANCES advances, and refuse more (enqueue's
 * -1). Not ported, as no template and no control reaches them: melody extraction and promotion, the alternating
 * phrases, the MIDI delay, replace-pattern, the accumulator, the gate ramp, the bass reharm; the state holds none of
 * their fields and the state text (bl_live_text) writes them as Swift's defaults (none). */
#pragma once
#include <stdint.h>
#include "bl.h"
#include "f64.h"
#include "music.h"
#include "legit.h"
#include "harmony.h"
#include "arrange.h"
#include "cc.h"
#include "project.h"

#define BL_MAX_PENDING 32       /* pendingChanges */
#define BL_MAX_RECALLS 2        /* of them recalls */
#define BL_MAX_ENABLES 8        /* pendingArrangementEnables */
#define BL_MAX_ADVANCES 8       /* pendingAdvances */
#define BL_MAX_TENSION 2        /* pendingTensionNotes: at most the budget (Project.tensionBudget nil: 2) */
#define BL_MAX_MORPHS (2 * BL_MAX_VOICES)     /* sceneMorphs: a voice's two CC targets */
#define BL_MAX_UPCOMING (BL_MAX_PENDING + BL_MAX_ENABLES + BL_MAX_ADVANCES)

/* StateSnapshot: the voices (patterns inside) and generations of an engine in project order, and the schedule */
typedef struct {
    bl_voice_t v[BL_MAX_VOICES];
    int64_t generation[BL_MAX_VOICES];
    bl_sched_t sched;
    uint8_t n;
} bl_state_t;

/* LiveEngine.Action, the ported ones */
enum {
    BL_A_TRANSPOSE, BL_A_REGENERATE, BL_A_BLANK, BL_A_LOCK, BL_A_RECALL, BL_A_KEY, BL_A_DENSITY, BL_A_BPART,
    BL_A_VOICE_ACTIVE, BL_A_TENSION, BL_A_SCENE, BL_A_ARRANGER, BL_A_ADVANCE
};

/* an action: voice a BL_VID_ (regenerate, blank, lock, voice active, tension), scene a BL_SCN_ (scene); arg the
 * degree offset (transpose), on 0/1 (lock, voice active, arranger) or the bars (scene); value the density; key the
 * key; state the recall's (copied at enqueue: the caller's may go) */
typedef struct {
    uint8_t kind, voice;
    int32_t arg;
    f64 value;
    bl_key_t key;
    const bl_state_t *state;
    uint8_t scene;
} bl_action_t;

/* -------------------------------------------------------- the engine --- */
/* VoiceState: request ticks -1 for none (a request is never below 0) */
typedef struct {
    bl_voice_t voice;
    bl_tick_t seg_start;            /* segmentStart */
    int64_t generation;
    uint8_t vid, blank, active;     /* vid: the voice's BL_VID_; blankActive; active */
    bl_tick_t blank_req, regen_req, nudge_req, accent_req;
} bl_vstate_t;

/* one of pendingChanges (GlobalChange at its bind tick): kind a BL_A_ (RECALL, KEY, DENSITY, BPART, VOICE_ACTIVE,
 * TENSION, SCENE), slot the recall's in bl_live.recall */
typedef struct {
    bl_tick_t tick;
    uint8_t kind, voice, scene, on, slot;
    int32_t bars;
    f64 value;
    bl_key_t key;
} bl_change_t;

/* one of sceneMorphs, keyed "voiceID|target": voice the project index, target the index in its CC map */
typedef struct {
    uint8_t voice, target;
    bl_morph_t m;
} bl_smorph_t;

/* LiveEngine. The fields are the engine's own; the app reads it through the functions below and places it (its
 * size is a test's report) */
typedef struct bl_live {
    bl_project_t project;           /* the running project: setKey rewrites its key */
    int32_t repeats;                /* legit.repeats (ambient: scaled at init) */
    f64 density;
    int32_t tension;                /* tensionRemaining */
    bl_sched_t sched;
    bl_vstate_t st[BL_MAX_VOICES];  /* states, in project order */
    uint8_t rank[BL_MAX_VOICES];    /* each voice's place in the string order of the ids: the notes' sort */
    bl_voice_t capped;              /* renderVoice's renderSource under the density cap */
    bl_tick_t rendered;             /* renderedUpTo */
    uint8_t nchange;
    bl_change_t change[BL_MAX_PENDING];         /* sorted by bind tick, stable */
    uint8_t recall_used[BL_MAX_RECALLS];
    bl_state_t recall[BL_MAX_RECALLS];
    uint8_t ntension;
    struct {
        bl_tick_t tick;
        uint8_t voice;              /* a BL_VID_ */
    } tension_note[BL_MAX_TENSION];
    uint8_t nmorph;
    bl_smorph_t morph[BL_MAX_MORPHS];
    uint8_t has_theme;
    bl_state_t theme;               /* themeSnapshot: the reprise */
    uint8_t has_arr;
    bl_arr_t arr;
    bl_tick_t arr_cursor;           /* arrangementCursor */
    uint8_t nadv, nen;
    bl_tick_t adv[BL_MAX_ADVANCES];
    struct {
        bl_tick_t tick;
        uint8_t on;
    } en[BL_MAX_ENABLES];
    bl_tick_t stop;                 /* pendingStopTick, -1 for none */
} bl_live_t;

/* LiveEngine(project:) */
void bl_live_init(bl_live_t *e, const bl_project_t *p);
/* enqueue(_:atTick:): 0, or -1 when the action's queue is full and it is dropped (Swift would keep it) */
int bl_live_enqueue(bl_live_t *e, const bl_action_t *a, bl_tick_t request);
/* render(range: lo..<hi): the notes sorted by (at, voice id, MIDI note), stably; windows must follow one another.
 * Returns their number and writes the first max of them (as snprintf; a window's notes are sorted where they are
 * written, so with a return above max the written ones may differ from Swift's first ones: give room for a window) */
int bl_live_render(bl_live_t *e, bl_tick_t lo, bl_tick_t hi, bl_note_t *out, int max);
/* renderCC(range: lo..<hi) of the same windows: per voice its lanes' events by (at, controller), then the scene
 * morphs by key. Returns and writes as bl_live_render (a voice's events are sorted where they are written) */
int bl_live_render_cc(const bl_live_t *e, bl_tick_t lo, bl_tick_t hi, bl_cc_t *out, int max);
/* captureState() */
void bl_live_capture(const bl_live_t *e, bl_state_t *out);
/* LiveEngine(project:).captureState() without an engine (the FM-1 has room for one): every voice materialized as
 * bl_live_init makes it (the ambient arranger's longer lifecycles too; the voices it holds back are in the state),
 * generation 1, the project's schedule. 0, or -1 for a project of more than BL_MAX_VOICES voices */
int bl_initial_state(const bl_project_t *p, bl_state_t *out);
/* pendingStopTick: the arranger's ending, where nothing renders any more and the transport stops; -1 for none */
bl_tick_t bl_live_stop_tick(const bl_live_t *e);
/* transpositionOffset(atBar:) */
int32_t bl_live_offset(const bl_live_t *e, int32_t bar);

/* ------------------------------------------------------ the UI mirror --- */
/* Snapshot.PendingRequest (blank before regenerate, as Swift reports them) */
enum { BL_REQ_NONE, BL_REQ_BLANK, BL_REQ_REGENERATE };
/* UpcomingEvent.Kind, the ported ones, and their raw values */
enum {
    BL_UP_KEY, BL_UP_RECALL, BL_UP_DENSITY, BL_UP_BPART, BL_UP_VOICE_ACTIVE, BL_UP_TENSION, BL_UP_ARRANGER,
    BL_UP_ADVANCE, BL_UP_SCENE, BL_NUPS
};
extern const char *const BL_UP_RAW[BL_NUPS];

/* UpcomingEvent: the detail as fields: root (key), value (density, unclamped), on (voiceActive, arranger), scene (a
 * BL_SCN_); voice a BL_VID_ (voiceActive, tension), else BL_VID_NONE */
typedef struct {
    bl_tick_t tick;
    uint8_t kind, voice, on, scene;
    int32_t root;
    f64 value;
} bl_upcoming_t;

/* Snapshot: per voice in project order; the upcoming events sorted by tick (stable: the global changes in queue
 * order, then the arranger's on/offs, then the advances); the arranger's status when there is one. The FM-1's own
 * (for the panel; on the Mac the Transport and the UI work them out): bar, the bar of renderedUpTo (the render
 * head's, Transport's mirror bar); scene, the scene (a BL_SCN_) every one of whose targets a scene morph heads for
 * (the first in the project's order), BL_SCN_NONE none */
typedef struct {
    uint8_t nvoices;
    struct {
        uint8_t vid, active, blank, pending, locked;   /* pending a BL_REQ_ */
        bl_tick_t pending_tick, length;
        int16_t remaining;                             /* -1: established */
        int64_t generation;
    } voice[BL_MAX_VOICES];
    uint8_t nup;
    bl_upcoming_t up[BL_MAX_UPCOMING];
    uint8_t has_arr, phase, nactive;
    uint8_t active[BL_MAX_VOICES];                     /* activeVoices: project indices */
    int32_t bars_in_phase, bars_until_change;
    f64 density;
    int32_t tension;
    bl_key_t key;
    int32_t bar;
    uint8_t scene;
} bl_status_t;

void bl_live_status(const bl_live_t *e, bl_status_t *out);

/* the canonical state text of tests/equiv/README.md ("The state hash"), one call of line per line (without its
 * "\n"), and its FNV-1a 64 (each line with its "\n") */
void bl_live_text(const bl_live_t *e, void (*line)(void *ctx, const char *text), void *ctx);
uint64_t bl_live_hash(const bl_live_t *e);
