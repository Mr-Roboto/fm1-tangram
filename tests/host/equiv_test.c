/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The equivalence runner: each tests/equiv/live_*.script played on the C engine (script.c) and its lines compared
 * with the committed .ref, line by line. Per script it prints ok or FAIL and the notes-hash, FNV-1a 64 over its N
 * lines, each with its "\n" (the value Task 12's scenario expects of the app); on the first difference the line
 * number, both lines and the last H line that matched. A script with `ratchet 0` is checked on its own too, whatever
 * its .ref holds: every note the C engine renders before its first load (the whole run when there is none) has
 * ratchet 1.
 *   equiv_test                    every tests/equiv/live_*.script
 *   equiv_test FILE...            these scripts
 *   equiv_test --text FILE        the lines with the state text (T) in place of each H, to diff with RefTool's */
#define _POSIX_C_SOURCE 200809L    /* strdup, opendir */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "script.h"

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf) buf[n] = 0;
    fclose(f);
    return buf;
}

static uint64_t fnv(uint64_t h, const char *s)
{
    for (; *s; s++) h = (h ^ (uint8_t)*s) * UINT64_C(0x100000001b3);
    return h;
}

/* Opt in with TANGRAM_REPRISE_DIAG=1 (comparison mode only). All diagnostics
 * go to stderr, never through emit()/compare(). The watcher runs AFTER a render
 * window, not inside capture/recall. Previous-window and post-window state must
 * not be mistaken for probes at those internal calls. Occurrences below are
 * unfiltered source candidates, not claims about the renderer's selected index.
 * In particular density, direction, mutation and recall may intervene. */
typedef struct {
    const char *path;
    int seen, phase, has_arr, has_theme, have_bass;
    bl_tick_t end, rendered, arr_cursor;
    bl_vstate_t bass;
    bl_sched_t sched;
    void (*previous_watch)(void *, const bl_live_t *, bl_tick_t);
} reprise_diag_t;

static void diag_config(const char *label, const bl_vconfig_t *v)
{
    fprintf(stderr, "  %s root=%d scale=%u count=%u intervals=", label,
            (int)v->key.root, v->key.scale.mode, v->key.scale.count);
    for (int i = 0; i < v->key.scale.count; i++) fprintf(stderr, "%s%d", i ? "," : "", v->key.scale.off[i]);
    fprintf(stderr, " gen=%u a=%d b=%d length=%u register=%u..%u direction=%u groove=%u phase=%u\n",
            v->gen.kind, v->gen.a, v->gen.b, (unsigned)v->length, v->reg_low, v->reg_high,
            v->gen.direction, v->gen.groove, v->phase);
}

static void diag_voice(const char *label, const bl_voice_t *v)
{
    const bl_pattern_t *p = &v->pat;
    fprintf(stderr, "  %s id=%s length=%lld source_offset=%lld n=%u gen=%u seed=%016llx"
            " remaining=%d locked=%u direction=%u cursor=%u pitch_phase=%u groove=%u dirty=%04x"
            " register=%u..%u referenceC=%d transpose=%u anchor=%u\n", label, v->id,
            (long long)p->length, (long long)p->start_offset, p->n, p->generator,
            (unsigned long long)p->seed, p->remaining, p->locked, p->direction,
            p->direction_cursor, p->pitch_phase, p->groove, v->config_dirty,
            v->reg_low, v->reg_high, (int)v->ref_c, v->participates, v->anchor);
    diag_config("active", &v->config);
    diag_config("pending", &v->pending);
    for (int i = 0; i < p->n; i++) {
        const bl_pnote_t *q = &p->note[i];
        fprintf(stderr, "    source[%d] position=%lld degree=%d octave=%d accidental=%d"
                " velocity=%u gate=%lld accent=%u ratchet=%u\n", i, (long long)q->position,
                (int)q->pitch.degree, (int)q->pitch.octave, (int)q->pitch.accidental,
                q->velocity, (long long)q->gate, q->accent, q->ratchet);
    }
}

static void diag_schedule(const char *label, const bl_sched_t *s)
{
    fprintf(stderr, "  %s schedule n=%u", label, s->n);
    for (int i = 0; i < s->n; i++) fprintf(stderr, " [%d:%d]", (int)s->ev[i].bar, (int)s->ev[i].offset);
    fputc('\n', stderr);
}

static void diag_live_bass(const char *label, const bl_vstate_t *s, bl_tick_t rendered)
{
    const bl_pattern_t *p = &s->voice.pat;
    fprintf(stderr, "  %s rendered=%lld segment_start=%lld next_completion=%lld generation=%lld"
            " active=%u blank=%u regen_req=%lld blank_req=%lld nudge_req=%lld accent_req=%lld\n",
            label, (long long)rendered, (long long)s->seg_start, (long long)(s->seg_start + p->length),
            (long long)s->generation, s->active, s->blank, (long long)s->regen_req,
            (long long)s->blank_req, (long long)s->nudge_req, (long long)s->accent_req);
    if (p->length > 0)
        fprintf(stderr, "    source_phase=%lld completion_phase=%lld\n",
                (long long)bl_floor_mod(rendered + p->start_offset, p->length),
                (long long)bl_floor_mod(rendered - s->seg_start, p->length));
    diag_voice(label, &s->voice);
}

static void diag_candidates(const char *label, const bl_pattern_t *p, bl_tick_t lo, bl_tick_t hi)
{
    bl_occ_t occ[BL_MAX_NOTES];
    if (p->length <= 0 || lo >= hi) return;
    int n = bl_occurrences(p, lo, hi, occ, BL_MAX_NOTES);
    fprintf(stderr, "  %s unfiltered-source-candidates [%lld,%lld) count=%d\n",
            label, (long long)lo, (long long)hi, n);
    for (int i = 0; i < n && i < BL_MAX_NOTES; i++)
        fprintf(stderr, "    at=%lld source_index=%u\n", (long long)occ[i].at, occ[i].index);
    if (n > BL_MAX_NOTES) fprintf(stderr, "    candidate listing truncated at %d\n", BL_MAX_NOTES);
}

/* the comparison as the run writes its lines */
typedef struct {
    char *ref;                      /* the .ref text, its lines ended in place */
    long line;                      /* the lines matched */
    long mismatch;                  /* the first that differs, 0 none */
    char got[300], want[300], last_h[64];
    uint64_t notes_hash;
    long notes;
    bl_tick_t ratchet0_until;       /* ratchet 0: the notes before this tick must not ratchet (-1: no such check) */
    long ratchet0_notes, ratchet0_bad;
    char ratchet0_first[300];       /* the first note that ratchets */
    reprise_diag_t *diag;           /* NULL leaves the original comparator untouched */
} cmp_t;

static void reprise_watch(void *ctx, const bl_live_t *e, bl_tick_t end)
{
    cmp_t *c = ctx;
    reprise_diag_t *d = c->diag;
    const bl_vstate_t *bass = NULL;
    for (int i = 0; i < e->project.nvoices; i++)
        if (!strcmp(e->st[i].voice.id, "bass")) bass = &e->st[i];
    int transition = d->seen && (d->has_arr != e->has_arr || (e->has_arr && d->phase != e->arr.phase));
    int capture = e->has_theme && (!d->seen || !d->has_theme);
    /* Absolute script ticks; only these two fixed mismatch regions are special.
     * An onset exactly at end belongs to the NEXT half-open render window. */
    int focus = (d->end <= 622080 && 622080 < end) || (d->end <= 917760 && 917760 < end);
    if (!d->seen || transition || capture || focus || c->mismatch) {
        fprintf(stderr, "DIAG case=%s lines_matched=%ld mismatch_line=%ld script_window=[%lld,%lld)"
                " engine_rendered=%lld previous_rendered=%lld arr=%u phase=%s previous_phase=%s"
                " bars_in_phase=%d bars_until_change=%d arr_cursor=%lld previous_cursor=%lld"
                " theme=%u reprise=%u density_bits=%016llx\n", d->path, c->line, c->mismatch,
                (long long)d->end, (long long)end, (long long)e->rendered, (long long)d->rendered,
                e->has_arr, e->has_arr ? BL_PHASE_RAW[e->arr.phase] : "off",
                d->seen && d->has_arr ? BL_PHASE_RAW[d->phase] : "unobserved/off",
                (int)e->arr.bars_in_phase, (int)e->arr.bars_until_change,
                (long long)e->arr_cursor, (long long)d->arr_cursor, e->has_theme,
                e->project.arr.reprise, (unsigned long long)e->density.bits);
        fprintf(stderr, "  transition_observed=%d theme_first_observed=%d focus_window=%d"
                " last_processed_bar_boundary=%lld (engine tick; not an internal recall probe)\n",
                transition, capture, focus, (long long)(e->arr_cursor - 1));
        /* In the focused 37/240/960-tick cases there is at most one bar
         * boundary per window. Establish that from the observed cursors rather
         * than assigning a transition to either historical mismatch tick. */
        if (transition && d->has_arr && e->has_arr && d->arr_cursor > 0 &&
            e->arr_cursor - d->arr_cursor == BL_TICKS_PER_BAR &&
            end - e->rendered == d->end - d->rendered)
            fprintf(stderr, "  single-bar transition_boundary_engine=%lld transition_boundary_script=%lld\n",
                    (long long)(e->arr_cursor - 1),
                    (long long)(e->arr_cursor - 1 + end - e->rendered));
        else if (transition)
            fprintf(stderr, "  transition boundary not uniquely located by these observations\n");
        if (c->mismatch) fprintf(stderr, "  actual=%s\n  reference=%s\n", c->got, c->want);
        if (d->have_bass) {
            diag_live_bass("previous-window bass", &d->bass, d->rendered);
            diag_schedule("previous-window", &d->sched);
            /* Only inspect the previous frame over a continuous engine window.
             * Loads can change the origin: do not silently mix script/engine ticks. */
            if (end - e->rendered == d->end - d->rendered)
                diag_candidates("previous frame", &d->bass.voice.pat, d->rendered, e->rendered);
        }
        if (bass) {
            diag_live_bass("post-window bass", bass, e->rendered);
            bl_tick_t lo = e->rendered - (end - d->end);
            diag_candidates("post frame", &bass->voice.pat, lo, e->rendered);
        }
        diag_schedule("post-window", &e->sched);
        if (e->has_theme) {
            for (int i = 0; i < e->theme.n; i++) if (!strcmp(e->theme.v[i].id, "bass")) {
                fprintf(stderr, "  theme bass saved_source_phase=%lld saved_completion_phase=%u"
                        " generation=%lld flags=%u (capture-relative, not runtime offset)\n",
                        (long long)e->theme.v[i].pat.start_offset, (unsigned)e->theme.motif_phase[i],
                        (long long)e->theme.generation[i], e->theme.motif_flags[i]);
                diag_voice("captured theme bass", &e->theme.v[i]);
            }
            diag_schedule("captured theme", &e->theme.sched);
        }
    }
    if (bass) d->bass = *bass;
    d->have_bass = bass != NULL;
    d->sched = e->sched;
    d->seen = 1;
    d->end = end;
    d->rendered = e->rendered;
    d->arr_cursor = e->arr_cursor;
    d->phase = e->arr.phase;
    d->has_arr = e->has_arr;
    d->has_theme = e->has_theme;
    if (d->previous_watch) d->previous_watch(ctx, e, end);
}

static int compare(void *ctx, const char *text)
{
    cmp_t *c = ctx;
    if (c->diag && text[0] == 'N') {
        long long tick;
        char voice[64];
        if (sscanf(text, "N %lld %63s", &tick, voice) == 2 && !strcmp(voice, "bass"))
            fprintf(stderr, "DIAG case=%s event_line=%ld actual-rendered=%s\n", c->diag->path, c->line + 1, text);
    }
    if (text[0] == 'N') {
        c->notes_hash = fnv(fnv(c->notes_hash, text), "\n");
        c->notes++;
        const char *r = strrchr(text, ' ');     /* N at voice midi vel gate accent ratchet */
        if (c->ratchet0_until >= 0 && strtoll(text + 2, NULL, 10) < c->ratchet0_until) {
            c->ratchet0_notes++;
            if (strcmp(r + 1, "1") && !c->ratchet0_bad++)
                snprintf(c->ratchet0_first, sizeof c->ratchet0_first, "%s", text);
        }
    }
    char *nl = c->ref ? strchr(c->ref, '\n') : NULL;
    const char *want = c->ref && *c->ref ? c->ref : "(end of the .ref)";
    if (nl) *nl = 0;
    if (!c->ref || !*c->ref || strcmp(text, want)) {
        c->mismatch = c->line + 1;
        snprintf(c->got, sizeof c->got, "%s", text);
        snprintf(c->want, sizeof c->want, "%s", want);
        if (c->diag) fprintf(stderr, "DIAG case=%s first_mismatch_line=%ld actual=%s reference=%s\n",
                             c->diag->path, c->mismatch, text, want);
        return 1;
    }
    c->line++;
    if (text[0] == 'H') snprintf(c->last_h, sizeof c->last_h, "%s", text);
    c->ref = nl ? nl + 1 : c->ref + strlen(c->ref);
    return 0;
}

static int print_line(void *ctx, const char *text)
{
    (void)ctx;
    printf("%s\n", text);
    return 0;
}

static int load_script(const char *path, bl_script_t *s)
{
    char err[200];
    char *text = read_file(path);
    if (!text) {
        printf("FAIL %s: cannot read\n", path);
        return -1;
    }
    int r = bl_script_parse(text, s, err, sizeof err);
    free(text);
    if (r) printf("FAIL %s: %s\n", path, err);
    return r;
}

/* one script against its .ref: 0 when equal */
static int run_one(const char *path)
{
    bl_script_t s;
    char ref_path[512], err[200] = "";
    if (load_script(path, &s)) return 1;
    snprintf(ref_path, sizeof ref_path, "%.*s.ref", (int)(strlen(path) - strlen(".script")), path);
    char *ref = read_file(ref_path);
    if (!ref) {
        printf("FAIL %s: cannot read %s\n", path, ref_path);
        bl_script_free(&s);
        return 1;
    }
    cmp_t c;
    memset(&c, 0, sizeof c);
    c.ref = ref;
    c.notes_hash = UINT64_C(0xcbf29ce484222325);
    snprintf(c.last_h, sizeof c.last_h, "(none)");
    c.ratchet0_until = -1;
    if (s.ratchet == 0) {
        c.ratchet0_until = INT64_MAX;
        for (int i = 0; i < s.nact && c.ratchet0_until == INT64_MAX; i++)
            if (s.act[i].kind == BL_SA_LOAD) c.ratchet0_until = s.act[i].tick;
    }
    const char *diag = getenv("TANGRAM_REPRISE_DIAG");
    if (diag && !strcmp(diag, "1")) {
        c.diag = calloc(1, sizeof *c.diag);
        if (c.diag) {
            c.diag->path = path;
            c.diag->previous_watch = bl_script_watch;
            bl_script_watch = reprise_watch;
            fprintf(stderr, "DIAG case=%s enabled: post-window inspection, unfiltered source candidates;"
                    " actual-rendered lines precede watcher; comparison still stops at first mismatch\n", path);
        } else fprintf(stderr, "DIAG case=%s unavailable: allocation failed\n", path);
    }
    int r = bl_script_play(&s, 0, compare, &c, err, sizeof err);
    if (c.diag) {
        bl_script_watch = c.diag->previous_watch;
        free(c.diag);
        c.diag = NULL;
    }
    int failed = 1;
    if (r) {
        printf("FAIL %s: %s (after line %ld)\n", path, err, c.line);
    } else if (c.mismatch) {
        printf("FAIL %s: line %ld\n  got  %s\n  want %s\n  last H that matched: %s\n", path, c.mismatch, c.got,
               c.want, c.last_h);
    } else if (c.ratchet0_until >= 0 && (c.ratchet0_bad || !c.ratchet0_notes)) {
        char before[32] = "the end";
        if (c.ratchet0_until < INT64_MAX) snprintf(before, sizeof before, "tick %lld", (long long)c.ratchet0_until);
        printf("FAIL %s: ratchet 0, yet %ld of the %ld notes before %s ratchet (the first: %s)\n", path,
               c.ratchet0_bad, c.ratchet0_notes, before, c.ratchet0_first);
    } else if (*c.ref) {
        char *nl = strchr(c.ref, '\n');
        if (nl) *nl = 0;
        printf("FAIL %s: line %ld\n  got  (the end of the run)\n  want %s\n  last H that matched: %s\n", path,
               c.line + 1, c.ref, c.last_h);
    } else {
        printf("ok   %s: %ld lines, %ld notes, notes-hash %016llx", path, c.line, c.notes,
               (unsigned long long)c.notes_hash);
        if (c.ratchet0_until >= 0) printf(", ratchet 0: none of %ld notes ratchets", c.ratchet0_notes);
        printf("\n");
        failed = 0;
    }
    free(ref);
    bl_script_free(&s);
    return failed;
}

static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--text")) {
        bl_script_t s;
        char err[200];
        if (load_script(argv[2], &s)) return 1;
        int r = bl_script_play(&s, 1, print_line, NULL, err, sizeof err);
        if (r) fprintf(stderr, "%s\n", err);
        bl_script_free(&s);
        return r ? 1 : 0;
    }
    int n = 0, failed = 0;
    char *paths[512];
    if (argc > 1) {
        for (int i = 1; i < argc && n < 512; i++) paths[n++] = strdup(argv[i]);
    } else {
        DIR *d = opendir("tests/equiv");
        struct dirent *de;
        while (d && (de = readdir(d)) && n < 512) {
            size_t len = strlen(de->d_name);
            if (strncmp(de->d_name, "live_", 5) || len < 7 || strcmp(de->d_name + len - 7, ".script")) continue;
            char buf[512];
            snprintf(buf, sizeof buf, "tests/equiv/%s", de->d_name);
            paths[n++] = strdup(buf);
        }
        if (d) closedir(d);
        qsort(paths, (size_t)n, sizeof paths[0], by_name);
    }
    for (int i = 0; i < n; i++) {
        failed += run_one(paths[i]);
        free(paths[i]);
    }
    printf("sizeof(bl_live_t) %zu, sizeof(bl_state_t) %zu\n", sizeof(bl_live_t), sizeof(bl_state_t));
    printf("%d scripts, %d failed\n", n, failed);
    return failed || !n ? 1 : 0;
}
