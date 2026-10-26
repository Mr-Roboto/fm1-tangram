/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The script parser and run (script.h), line for line RefTool's Script and Run (tools/reference/Sources/RefTool/
 * Script.swift) and its reference lines (Dump.swift). The app project (AppServices.project) is kept apart from the
 * running engine's, as on the Mac. Its file-scope names start with scr_ (SCR_): the app's host build includes this
 * file in its unity build (app/host.c), beside fm1-base's simulator. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "script.h"

#define SCR_SLOTS 32                /* UIState.snapshotSlots */
#define SCR_HASH_PERIOD (16 * BL_TICKS_PER_BAR)
#define SCR_MAX_WAITING 256         /* actions between two wakes */
#define SCR_MAX_STATES 16           /* of them recalls and reseeds, each with its state */
#define SCR_MAX_NOTES 16384         /* a window's notes and CC */

/* ------------------------------------------------------------- parsing --- */
static int scr_is_integer(const char *w)
{
    if (*w == '-') w++;
    if (!*w) return 0;
    for (; *w; w++)
        if (*w < '0' || *w > '9') return 0;
    return 1;
}

static int scr_parse_seed(const char *w, uint64_t *out)
{
    if (w[0] != '0' || (w[1] != 'x' && w[1] != 'X')) return 0;
    w += 2;
    size_t n = strlen(w);
    if (n < 1 || n > 16) return 0;
    uint64_t v = 0;
    for (; *w; w++) {
        int d = *w >= '0' && *w <= '9' ? *w - '0' : *w >= 'a' && *w <= 'f' ? *w - 'a' + 10
              : *w >= 'A' && *w <= 'F' ? *w - 'A' + 10 : -1;
        if (d < 0) return 0;
        v = v << 4 | (uint64_t)d;
    }
    *out = v;
    return 1;
}

static int scr_word_index(const char *const *names, int n, const char *w)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(names[i], w)) return i;
    return -1;
}

/* Script.action: 1 parsed, 0 a bad action, -1 an unknown voice */
static int scr_parse_action(char **w, int n, bl_sact_t *a)
{
    const char *k = w[0];
    long v;
    if (n == 2 && !strcmp(k, "transpose") && scr_is_integer(w[1])) {
        a->kind = BL_SA_TRANSPOSE;
        a->a = (int32_t)strtol(w[1], NULL, 10);
    } else if ((n == 2 && (!strcmp(k, "regenerate") || !strcmp(k, "blank"))) || (n == 3 && !strcmp(k, "lock"))
               || (n == 3 && !strcmp(k, "voice"))) {
        int vid = bl_vid(w[1]);
        if (vid == BL_VID_NONE) return -1;
        a->voice = (uint8_t)vid;
        if (!strcmp(k, "regenerate")) a->kind = BL_SA_REGENERATE;
        else if (!strcmp(k, "blank")) a->kind = BL_SA_BLANK;
        else if (!strcmp(k, "lock")) {
            if (strcmp(w[2], "0") && strcmp(w[2], "1")) return 0;
            a->kind = BL_SA_LOCK;
            a->a = w[2][0] == '1';
        } else {
            if (strcmp(w[2], "on") && strcmp(w[2], "off")) return 0;
            a->kind = BL_SA_VOICE;
            a->a = !strcmp(w[2], "on");
        }
    } else if (n == 1 && !strcmp(k, "reroll")) {
        a->kind = BL_SA_REROLL;
    } else if (n == 2 && (!strcmp(k, "save") || !strcmp(k, "recall"))) {
        if (!scr_is_integer(w[1]) || (v = strtol(w[1], NULL, 10)) < 0 || v >= SCR_SLOTS) return 0;
        a->kind = !strcmp(k, "save") ? BL_SA_SAVE : BL_SA_RECALL;
        a->a = (int32_t)v;
    } else if (n == 3 && !strcmp(k, "key")) {
        int mode = scr_word_index(BL_MODE_RAW, BL_NMODES, w[2]);
        if (!scr_is_integer(w[1]) || (v = strtol(w[1], NULL, 10)) < 0 || v > 11 || mode < 0) return 0;
        a->kind = BL_SA_KEY;
        a->a = (int32_t)v;
        a->b = mode;
    } else if (n == 2 && !strcmp(k, "density")) {
        if (!scr_is_integer(w[1]) || (v = strtol(w[1], NULL, 10)) < 0 || v > 100) return 0;
        a->kind = BL_SA_DENSITY;
        a->a = (int32_t)v;
    } else if (n == 1 && !strcmp(k, "bpart")) {
        a->kind = BL_SA_BPART;
    } else if (n == 1 && !strcmp(k, "tension")) {
        a->kind = BL_SA_TENSION;
    } else if (n == 2 && !strcmp(k, "scene")) {
        if (!scr_is_integer(w[1]) || (v = strtol(w[1], NULL, 10)) < 0) return 0;
        a->kind = BL_SA_SCENE;
        a->a = (int32_t)v;
    } else if (n == 2 && !strcmp(k, "arranger")) {
        if (strcmp(w[1], "on") && strcmp(w[1], "off")) return 0;
        a->kind = BL_SA_ARRANGER;
        a->a = !strcmp(w[1], "on");
    } else if (n == 1 && !strcmp(k, "advance")) {
        a->kind = BL_SA_ADVANCE;
    } else if (n == 2 && !strcmp(k, "reseed")) {
        if (!scr_parse_seed(w[1], &a->seed)) return 0;
        a->kind = BL_SA_RESEED;
    } else if (n == 3 && !strcmp(k, "load")) {
        int t = scr_word_index(BL_TPL_NAME, BL_NTPL, w[1]);
        if (t < 0 || !scr_parse_seed(w[2], &a->seed)) return 0;
        a->kind = BL_SA_LOAD;
        a->tpl = (uint8_t)t;
    } else {
        return 0;
    }
    return 1;
}

int bl_script_parse(const char *text, bl_script_t *s, char *err, int errmax)
{
    memset(s, 0, sizeof *s);
    s->arranger = s->density = -1;
    int seen[6] = {0}, line = 0;
    bl_tick_t last = 0, last_mute = 0;
    static const char *const HEADS[6] = {"template", "seed", "arranger", "density", "window", "bars"};
    static const char *const ARRANGERS[4] = {"off", "classic", "slow", "ambient"};
    const char *p = text;
    while (*p) {
        char buf[512], *w[8];
        int n = 0, len = 0;
        line++;
        while (*p && *p != '\n') {
            if (len < (int)sizeof buf - 1) buf[len++] = *p;
            p++;
        }
        if (*p) p++;
        buf[len] = 0;
        char *hash = strchr(buf, '#');
        if (hash) *hash = 0;
        for (char *t = strtok(buf, " \t\r"); t && n < 8; t = strtok(NULL, " \t\r")) w[n++] = t;
        if (!n) continue;
        if (!strcmp(w[0], "at")) {
            if (n < 3 || !scr_is_integer(w[1]) || strtoll(w[1], NULL, 10) < 0) {
                snprintf(err, (size_t)errmax, "line %d: expected 'at TICK ACTION...'", line);
                return -1;
            }
            bl_sact_t a;
            memset(&a, 0, sizeof a);
            a.tick = strtoll(w[1], NULL, 10);
            if (a.tick < last) {
                snprintf(err, (size_t)errmax, "line %d: ticks must not decrease", line);
                return -1;
            }
            last = a.tick;
            int ok = scr_parse_action(w + 2, n - 2, &a);
            if (ok <= 0) {
                snprintf(err, (size_t)errmax, ok < 0 ? "line %d: unknown voice" : "line %d: bad action", line);
                return -1;
            }
            if (s->nact == s->cap) {
                s->cap = s->cap ? 2 * s->cap : 64;
                s->act = realloc(s->act, (size_t)s->cap * sizeof *s->act);
            }
            s->act[s->nact++] = a;
            continue;
        }
        if (!strcmp(w[0], "mute")) {                    /* the runtime's (script.h): parsed, never played here */
            bl_smute_t m;
            if (n != 4 || !scr_is_integer(w[2]) || strtoll(w[2], NULL, 10) < 0
                || (strcmp(w[3], "0") && strcmp(w[3], "1"))) {
                snprintf(err, (size_t)errmax, "line %d: expected 'mute V TICK 0|1'", line);
                return -1;
            }
            m.voice = (uint8_t)bl_vid(w[1]);
            m.tick = strtoll(w[2], NULL, 10);
            m.on = w[3][0] == '1';
            if (m.voice == BL_VID_NONE || m.tick < last_mute) {
                snprintf(err, (size_t)errmax, m.voice == BL_VID_NONE ? "line %d: unknown voice"
                         : "line %d: mute ticks must not decrease", line);
                return -1;
            }
            last_mute = m.tick;
            if (s->nmute == s->mcap) {
                s->mcap = s->mcap ? 2 * s->mcap : 16;
                s->mute = realloc(s->mute, (size_t)s->mcap * sizeof *s->mute);
            }
            s->mute[s->nmute++] = m;
            continue;
        }
        int h = scr_word_index(HEADS, 6, w[0]);
        if (h < 0 || n != 2) {
            snprintf(err, (size_t)errmax, "line %d: unknown line or expected '%s VALUE'", line, w[0]);
            return -1;
        }
        if (seen[h]++) {
            snprintf(err, (size_t)errmax, "line %d: '%s' given twice", line, w[0]);
            return -1;
        }
        long v = scr_is_integer(w[1]) ? strtol(w[1], NULL, 10) : -1;
        int bad = 0;
        switch (h) {
        case 0: bad = (s->tpl = scr_word_index(BL_TPL_NAME, BL_NTPL, w[1])) < 0; break;
        case 1: bad = !scr_parse_seed(w[1], &s->seed); s->has_seed = 1; break;
        case 2: bad = (s->arranger = scr_word_index(ARRANGERS, 4, w[1])) < 0; break;
        case 3: bad = v < 0 || v > 100; s->density = (int)v; break;
        case 4: bad = v < 1; s->window = v; break;
        default: bad = v < 1; s->bars = (int32_t)v; break;
        }
        if (bad) {
            snprintf(err, (size_t)errmax, "line %d: bad value '%s'", line, w[1]);
            return -1;
        }
    }
    if (!seen[0] || !seen[4] || !seen[5]) {
        snprintf(err, (size_t)errmax, "no 'template', 'window' or 'bars' line");
        return -1;
    }
    if (s->nact && s->act[s->nact - 1].tick >= (bl_tick_t)s->bars * BL_TICKS_PER_BAR) {
        snprintf(err, (size_t)errmax, "an action is not before the end");
        return -1;
    }
    return 0;
}

void bl_script_free(bl_script_t *s)
{
    free(s->act);
    free(s->mute);
    s->act = NULL;
    s->mute = NULL;
}

/* Double(K) / 100 clamped: the density of `density K` */
static f64 scr_density_of(int32_t k)
{
    f64 v = f64_div(f64_from_i64(k), F64(0x4059000000000000));                  /* 100.0 */
    return f64_max(F64(0x0000000000000000), f64_min(F64(0x3FF0000000000000), v));
}

/* Script.initialProject */
void bl_script_project(const bl_script_t *s, bl_project_t *p)
{
    bl_template(s->tpl, p);
    if (s->has_seed) p->seed = s->seed;
    switch (s->arranger) {
    case 0:                                             /* (arrangement ?? ArrangementConfig()), enabled false */
        if (!p->has_arr) p->arr = (bl_arrcfg_t){0, 0, 0, 8, 16};
        p->has_arr = 1;
        p->arr.enabled = 0;
        break;
    case 1: p->has_arr = 1; p->arr = bl_arr_classic; break;
    case 2: p->has_arr = 1; p->arr = bl_arr_slow; break;
    case 3: p->has_arr = 1; p->arr = bl_arr_ambient; break;
    }
    if (s->density >= 0) p->density = scr_density_of(s->density);
}

/* ----------------------------------------------------------------- run --- */
typedef struct {
    const bl_script_t *s;
    bl_project_t app;               /* AppServices.project */
    bl_project_t pending;           /* Transport.pendingProject */
    int has_pending, stop_shown;
    bl_live_t engine, fresh;        /* the running engine; reseed's LiveEngine(project: p) */
    bl_tick_t origin;
    int nwait, nstates;
    bl_action_t wait[SCR_MAX_WAITING];  /* Transport.pendingActions */
    bl_state_t states[SCR_MAX_STATES];  /* their states */
    uint8_t slot_used[SCR_SLOTS];
    bl_state_t slot[SCR_SLOTS];
    bl_note_t notes[SCR_MAX_NOTES];
    bl_cc_t cc[SCR_MAX_NOTES];
    char *err;
    int errmax;
} scr_run_t;

static int scr_waiting(scr_run_t *r, bl_action_t a)
{
    if (r->nwait == SCR_MAX_WAITING) {
        snprintf(r->err, (size_t)r->errmax, "more than %d actions between two wakes", SCR_MAX_WAITING);
        return -1;
    }
    r->wait[r->nwait++] = a;
    return 0;
}

/* a waiting recall with its own copy of the state (Swift's action holds the value) */
static bl_state_t *scr_new_state(scr_run_t *r)
{
    if (r->nstates == SCR_MAX_STATES) {
        snprintf(r->err, (size_t)r->errmax, "more than %d recalls between two wakes", SCR_MAX_STATES);
        return NULL;
    }
    return &r->states[r->nstates++];
}

/* Run.apply: one action as the app performs it; engine actions wait for the next wake, reads of the engine see the
 * last window's state */
static int scr_apply(scr_run_t *r, const bl_sact_t *s)
{
    bl_action_t a;
    memset(&a, 0, sizeof a);
    a.voice = s->voice;
    a.arg = s->a;
    switch (s->kind) {
    case BL_SA_TRANSPOSE: a.kind = BL_A_TRANSPOSE; return scr_waiting(r, a);
    case BL_SA_REGENERATE: a.kind = BL_A_REGENERATE; return scr_waiting(r, a);
    case BL_SA_REROLL: {                                /* UIState.rerollAllUnlocked */
        static bl_status_t st;
        bl_live_status(&r->engine, &st);
        a.kind = BL_A_REGENERATE;
        for (int i = 0; i < st.nvoices; i++) {
            if (st.voice[i].locked) continue;
            a.voice = st.voice[i].vid;
            if (scr_waiting(r, a)) return -1;
        }
        return 0;
    }
    case BL_SA_BLANK: a.kind = BL_A_BLANK; return scr_waiting(r, a);
    case BL_SA_LOCK: a.kind = BL_A_LOCK; return scr_waiting(r, a);
    case BL_SA_SAVE:                                    /* Transport.captureState() */
        bl_live_capture(&r->engine, &r->slot[s->a]);
        r->slot_used[s->a] = 1;
        return 0;
    case BL_SA_RECALL: {                                /* an empty slot does nothing */
        if (!r->slot_used[s->a]) return 0;
        bl_state_t *st = scr_new_state(r);
        if (!st) return -1;
        *st = r->slot[s->a];
        a.kind = BL_A_RECALL;
        a.state = st;
        return scr_waiting(r, a);
    }
    case BL_SA_KEY:                                     /* AppServices.applyLiveMusicalChanges */
        a.kind = BL_A_KEY;
        a.key.root = s->a;
        bl_scale_preset(s->b, &a.key.scale);
        r->app.key = a.key;
        return scr_waiting(r, a);
    case BL_SA_DENSITY:                                 /* AppServices.setDensity */
        a.kind = BL_A_DENSITY;
        a.value = scr_density_of(s->a);
        r->app.density = a.value;
        return scr_waiting(r, a);
    case BL_SA_BPART: a.kind = BL_A_BPART; return scr_waiting(r, a);
    case BL_SA_VOICE: a.kind = BL_A_VOICE_ACTIVE; return scr_waiting(r, a);
    case BL_SA_TENSION: {                               /* UIState.spendTension: the pad, else the first voice */
        int t = -1;
        for (int i = 0; i < r->app.nvoices && t < 0; i++)
            if (r->app.voice[i].role == BL_ROLE_PAD) t = i;
        if (t < 0 && r->app.nvoices) t = 0;
        if (t < 0) return 0;
        a.kind = BL_A_TENSION;
        a.voice = (uint8_t)bl_vid(r->app.voice[t].id);
        return scr_waiting(r, a);
    }
    case BL_SA_SCENE:                                   /* UIState.morphToScene: the app project's n-th, 8 bars */
        if (s->a >= r->app.nscenes) return 0;
        a.kind = BL_A_SCENE;
        a.scene = (uint8_t)bl_scn(r->app.scene[s->a].name);
        a.arg = 8;
        return scr_waiting(r, a);
    case BL_SA_ARRANGER:                                /* AppServices.setArrangerEnabled */
        if (!r->app.has_arr) r->app.arr = (bl_arrcfg_t){0, 0, 0, 8, 16};
        r->app.has_arr = 1;
        r->app.arr.enabled = (uint8_t)s->a;
        a.kind = BL_A_ARRANGER;
        return scr_waiting(r, a);
    case BL_SA_ADVANCE: a.kind = BL_A_ADVANCE; return scr_waiting(r, a);
    case BL_SA_RESEED: {                                /* AppServices.reseed */
        bl_state_t *st = scr_new_state(r);
        if (!st) return -1;
        r->app.seed = s->seed;
        bl_live_init(&r->fresh, &r->app);
        bl_live_capture(&r->fresh, st);
        a.kind = BL_A_RECALL;
        a.state = st;
        return scr_waiting(r, a);
    }
    case BL_SA_LOAD:                                    /* AppServices.applyTemplate, Transport.loadProject */
        bl_template(s->tpl, &r->app);
        r->app.seed = s->seed;
        r->pending = r->app;
        r->has_pending = 1;
        return 0;
    }
    return 0;
}

static const char *scr_voice_id(const scr_run_t *r, int i) { return r->engine.project.voice[i].id; }

/* the T lines of --text: "T " and the state text's line */
typedef struct {
    bl_emit_fn emit;
    void *ctx;
    int stopped;
} scr_text_t;

static void scr_text_line(void *ctx, const char *text)
{
    scr_text_t *t = ctx;
    char line[300];
    if (t->stopped) return;
    snprintf(line, sizeof line, "T %s", text);
    t->stopped = t->emit(t->ctx, line);
}

/* Run.play */
int bl_script_play(const bl_script_t *s, int text, bl_emit_fn emit, void *ctx, char *err, int errmax)
{
    scr_run_t *r = calloc(1, sizeof *r);
    if (!r) {
        snprintf(err, (size_t)errmax, "out of memory");
        return -1;
    }
    r->s = s;
    r->err = err;
    r->errmax = errmax;
    bl_script_project(s, &r->app);
    bl_live_init(&r->engine, &r->app);
    bl_tick_t total = (bl_tick_t)s->bars * BL_TICKS_PER_BAR, head = 0;
    int next = 0, result = 0, stopped = 0;
    char line[160];
    while (head < total && !stopped) {
        /* the actions at this tick, in script order, between two wakes */
        while (next < s->nact && s->act[next].tick == head)
            if (scr_apply(r, &s->act[next++])) goto fail;
        /* the wake: an armed project takes over on its bar line, then the actions bind at the render head */
        bl_tick_t boundary = -1;
        if (r->has_pending) {
            bl_tick_t b = r->origin + (head - r->origin + BL_TICKS_PER_BAR - 1) / BL_TICKS_PER_BAR * BL_TICKS_PER_BAR;
            if (head == b) {
                bl_live_init(&r->engine, &r->pending);
                r->origin = b;
                r->has_pending = 0;
                r->stop_shown = 0;
            } else {
                boundary = b;
            }
        }
        for (int i = 0; i < r->nwait; i++)
            if (bl_live_enqueue(&r->engine, &r->wait[i], head - r->origin)) {
                snprintf(err, (size_t)errmax, "tick %lld: the engine's queue is full (action kind %d)",
                         (long long)head, r->wait[i].kind);
                goto fail;
            }
        r->nwait = r->nstates = 0;
        /* the window: to the next multiple of the window, the next action, the armed project's boundary, the end */
        bl_tick_t end = (head / s->window + 1) * s->window;
        if (end > total) end = total;
        if (next < s->nact && s->act[next].tick < end) end = s->act[next].tick;
        if (boundary >= 0 && boundary < end) end = boundary;
        bl_tick_t lo = head - r->origin, hi = end - r->origin;
        int n = bl_live_render(&r->engine, lo, hi, r->notes, SCR_MAX_NOTES);
        if (n > SCR_MAX_NOTES) {
            snprintf(err, (size_t)errmax, "tick %lld: %d notes in one window", (long long)head, n);
            goto fail;
        }
        for (int i = 0; i < n && !stopped; i++) {
            const bl_note_t *q = &r->notes[i];
            snprintf(line, sizeof line, "N %lld %s %d %d %lld %d %d", (long long)(q->at + r->origin),
                     scr_voice_id(r, q->voice), q->midi, q->velocity, (long long)q->gate, q->accent, q->ratchet);
            stopped = emit(ctx, line);
        }
        int m = bl_live_render_cc(&r->engine, lo, hi, r->cc, SCR_MAX_NOTES);
        if (m > SCR_MAX_NOTES) {
            snprintf(err, (size_t)errmax, "tick %lld: %d CC in one window", (long long)head, m);
            goto fail;
        }
        for (int i = 0; i < m && !stopped; i++) {
            const bl_cc_t *c = &r->cc[i];
            snprintf(line, sizeof line, "C %lld %s %d %d", (long long)(c->at + r->origin), scr_voice_id(r, c->voice),
                     c->controller, c->value);
            stopped = emit(ctx, line);
        }
        bl_tick_t stop = bl_live_stop_tick(&r->engine);
        if (!stopped && !r->stop_shown && stop >= 0) {
            snprintf(line, sizeof line, "S %lld", (long long)(stop + r->origin));
            stopped = emit(ctx, line);
            r->stop_shown = 1;
        }
        if (!stopped && end / SCR_HASH_PERIOD > head / SCR_HASH_PERIOD) {
            if (text) {
                scr_text_t t = {emit, ctx, 0};
                bl_live_text(&r->engine, scr_text_line, &t);
                stopped = t.stopped;
            } else {
                snprintf(line, sizeof line, "H %lld %016llx", (long long)end,
                         (unsigned long long)bl_live_hash(&r->engine));
                stopped = emit(ctx, line);
            }
        }
        head = end;
        /* the arranger's ending: the transport stops once the head reaches the stop tick */
        if (stop >= 0 && head >= stop + r->origin) break;
    }
    goto done;
fail:
    result = -1;
done:
    free(r);
    return result;
}

