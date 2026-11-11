/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): Berlinan's checks for
 * "expect", its commands and a reboot.
 *
 * Berlinan's checks (fm1-base's own: store_writes clocks_out midi_out ...):
 *   playing tick head        the transport: playing; the sounding tick; the render head
 *   tick_rate bpm tempo_tick the clock: thousandths of a tick per ms (INT: bpm x 16; EXT: as measured); the tempo
 *                            (Transport.currentBPM); where the last tempo segment began (-1: none yet)
 *   queued sounding dropped  the timed events waiting; the notes the registry holds sounding (soundingC: on channel
 *                            C, 0-15; soundingC.N: note N there, 0 or 1); events a full queue refused (and notes or
 *                            CC past a window's buffer)
 *   per_ms                   the most messages the app sent in one ms
 *   dumpcount dumpbad        the last dumpcount's lines; the dumpis lines that did not hold
 *   midicmp midilines        the last cmpmidi: 0 the same, else the first line that differs (-1: a file missing);
 *                            the lines that matched
 *   selfnotes selfhash_hi selfhash_lo   the last selfrender
 *   tpl root mode bpm_proj seed_hi seed_lo seeded chanI density_pct has_density arr_has arr_on arr_ambient form
 *      ratchet sync ext_ports lights save_pending save_lit save_unsent
 *                            the app project (proj: seeded 0 while the defaults' seed is still to be drawn; chanI its
 *                            voice I's channel; form proj_form, a FORM_); a SAVE waiting for the stop; the SAVE
 *                            button's LED (blinking while one waits); what the transport still had to send at once (a
 *                            stop's Stop and offs) when the last SAVE wrote (-1: none yet)
 *   slot_usedK slot_litK undo_depth stale   slot K (0-5) stored; its key lit; the undo points; a change made while
 *                            playing that the transport has not got yet (transportStale)
 *   pat_hash_hi pat_hash_lo  FNV-1a 64 over the transport's patterns (as captureState gives them: the running
 *                            engine's at the head; before a Play, the project's initial ones), their notes and lengths
 *                            only, as text: per voice "ID LENGTH N\n", then per note "POSITION GATE DEGREE OCTAVE
 *                            ACCIDENTAL VELOCITY ACCENT RATCHET\n"
 *   slot_patK_hi slot_patK_lo   the same over slot K's state
 *   ratchets eng_ratchet     the notes that ratchet (ratchet > 1) in the transport's patterns (as pat_hash's); the
 *                            ratchet probability of the running engine's project (stopped: the transport's project,
 *                            the next Play's), in % rounded, -1 for nil (0.15)
 *   view voice_sel help warns   the panel (ui.c): the page (0 Perform ... 6 Setup); the selected voice (its key);
 *                            the button whose card is up, plus one (0 none); what was refused and said
 *   degree pending_degree    the degree in force at the render head's bar (1 + the offset); the next bar's when it
 *                            differs, else 0 (stopped: 1, 0)
 *   mutedV activeV lockedV genV reqV blankV   voice key V (0-6: Pluck 1, Pluck 2, Bass, Pad, Lead, Drone,
 *                            Heartbeat) in the transport and the mirror (the running engine's snapshot): muted; 1
 *                            playing, 0 held back (WAITS), -1 not in the running engine (stopped: 1 in the template);
 *                            locked; its generation, its pending request (0 none, 1 blank, 2 regenerate) and its
 *                            blank cycle (-1 for each: not there)
 *   tension_left arr_phase   the tension left (stopped: the budget, 2); the arranger's phase (-1 none, stopped)
 *   key_ledK btn_ledB        key K's light as last set: 0 off, 1 dim, 2 lit; button B's (a B_ number: 0 FX ... 13
 *                            OCT+), 0 or 1
 *   tension_voice            the voice the first pending tension lands on, as its key (-1 none)
 *   lanes lcm_tenths lcm_secs   the pages' voice lanes (the running engine's voices; stopped, the transport's
 *                            preview); their convergence as the Perform page shows it, read back from its text: the
 *                            bars in tenths and the seconds at the tempo shown (-1 for both: INF). lcm_tenthsP and
 *                            lcm_secsP: the same for a period of P ticks (-1: Convergence.period's nil)
 *   note_check deg_label_max the note names (C3 = 60) and registers, shown and said, against a table: the misses;
 *                            the Pattern page's highest degree label for the selected voice (in the key sounding)
 *   bpm_shown                the tempo the pages show and say (UIState's liveBPM ?? project.bpm)
 *   delay_msV delay_check    the Delay page's time for the template's voice V (DelayHint, ms); DelayHint's integers
 *                            against Swift's (60000.0 / bpm * beats).rounded() in f64, every tempo 40-220 and each
 *                            division: how many differ
 * The speakdump: every page (the Voice, Pattern and CC pages with each voice selected), knob value, help card and
 * message, as screen.c's speak_dump prints them.
 * Its commands:
 *   selfrender TEMPLATE BARS (stopped) the template on its own seed, rendered by the app's engine for BARS bars in
 *                            windows of 240 ticks, from tick 0 to the end or the arranger's ending; selfnotes its
 *                            notes, selfhash_hi/_lo the two halves of their notes-hash: FNV-1a 64 over the N lines of
 *                            tests/equiv/README.md, each with its "\n", the value tests/host/equiv_test.c prints
 *   selfscript FILE          (stopped) a .midiscript for the next Play: its project, its actions as rt_action at
 *                            their ticks (a window ends at each), its mutes as muteat, the render held at its end.
 *                            Its actions: those that are one engine action each, and load (rt_load); not reroll,
 *                            save, recall, tension, scene, reseed
 *   muteat VOICE TICK 0|1    a test's mute: for the windows that start at or after TICK, nothing flushed (the rule
 *                            of RefTool's midi mode)
 *   racefc                   the next start (an FA's: app_loop) meets an FC from TRS, the tick run once (as its
 *                            interrupt) between app_loop's look and the start's lock
 *   dumpmidi [FILE]          start (FILE) or stop logging "tick status d1 d2" (hex bytes) for every message the
 *                            transport sends, in send order; a flush's messages carry the sounding tick
 *   cmpmidi FILE             the dump so far against FILE (a .midiref), line by line: midicmp, midilines
 *                            TANGRAM_MIDI_DIAG=1 adds full-stream alignment on stderr only; normal comparison,
 *                            assertions and status are unchanged. No MIDI events are filtered or normalized.
 *   dumpcount ST D1 D2 [TICK]   the dump's lines with these bytes (hex, * any; at TICK, if given): expect dumpcount
 *   dumpis K ST D1 D2        the dump's K-th line from its end (1 the last) has these bytes; else a FAIL line and
 *                            dumpbad one more
 *   rt play | stop | tempo BPM | load TEMPLATE [SEED] | mute VOICE 0|1 | channel VOICE CH | sync int|ext |
 *      port USB|TRS|both     the transport's controls (VOICE a voice id, CH 0-15).
 *                            sync/port edit persistent GLOBAL device settings, not the project;
 *                            sync is stopped-only, port edits are live. Persistence remains deferred.
 *   rt burst N               (playing) N note-ons on channel 16 (notes 0..N-1, velocity 1) due now and their offs a
 *                            tick later, into the queue: more than a ms may send
 *   proj template TEMPLATE [SEED] | key ROOT MODE | tempo BPM | density K | arranger on|off |
 *      form classic|slow|ambient | ambient on|off | channel VOICE CH | sync int|ext | ratchet PERCENT | dice
 *                            the project's changes as the Mac's AppServices makes them (project.c: MODE a ModeID raw
 *                            value, K 0-100; SEED a test's in place of the fresh one), until the panel has them all.
 *                            `proj sync` is a compatibility alias for global sync, never a project field.
 *                            `proj ratchet` accepts decimal 0..50 in steps of 5, no extra arguments;
 *                            stopped: applied now; playing: a difference waits for the next Play/template takeover.
 *   proj format1             write an unsupported-format record for rejection/default checks, not migration
 *   slot store K | slot recall K | undo push | undo pop | save
 *                            the slots (K 0-5), the undo stack, SAVE */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../tests/host/script.c"        /* the script parser, equiv_test's */

static int self_notes = -1;              /* the last selfrender's notes, -1 none (or it failed) */
static int save_unsent = -1;             /* what was still to go out at once when the last SAVE wrote, -1 none yet */
static uint64_t self_hash;

static uint64_t fnv(uint64_t h, const char *s)
{
    for (; *s; s++)
        h = (h ^ (uint8_t)*s) * UINT64_C(0x100000001b3);
    return h;
}

static int tpl_of(const char *name)      /* a BL_TPL_, BL_NTPL for none */
{
    int t = 0;
    while (t < BL_NTPL && strcmp(BL_TPL_NAME[t], name))
        t++;
    return t;
}

static void selfrender(const char *tpl, const char *bars)
{
    char line[160];
    int t = tpl_of(tpl), i, n;
    bl_tick_t total = (bl_tick_t)atoi(bars) * BL_TICKS_PER_BAR, head, end, stop;
    self_notes = -1;
    if (t == BL_NTPL || total < 1 || rt_playing()) {
        printf("FAIL selfrender %s %s: no such template, no bars, or playing\n", tpl, bars);
        return;
    }
    bl_template(t, &rt_next);            /* (the pending project's room: nothing is pending while stopped) */
    bl_live_init(&rt_eng, &rt_next);     /* the transport's engine (Play makes its own) */
    rt.fresh = 1;                        /* (it is not the project's: captureState gives the project's preview) */
    self_notes = 0;
    self_hash = UINT64_C(0xcbf29ce484222325);
    for (head = 0; head < total; head = end) {
        end = (head / RT_WINDOW + 1) * RT_WINDOW;
        if (end > total)
            end = total;
        n = bl_live_render(&rt_eng, head, end, rt_note, RT_NOTES);
        if (n > RT_NOTES || bl_live_render_cc(&rt_eng, head, end, rt_cc, RT_CC) > RT_CC) {
            printf("FAIL selfrender: tick %lld: more notes or CC than a window holds\n", (long long)head);
            self_notes = -1;
            return;
        }
        for (i = 0; i < n; i++) {
            const bl_note_t *q = &rt_note[i];
            snprintf(line, sizeof line, "N %lld %s %d %d %lld %d %d\n", (long long)q->at,
                     rt_eng.project.voice[q->voice].id, q->midi, q->velocity, (long long)q->gate, q->accent,
                     q->ratchet);
            self_hash = fnv(self_hash, line);
            self_notes++;
        }
        stop = bl_live_stop_tick(&rt_eng);   /* the arranger's ending: the transport stops there */
        if (stop >= 0 && end >= stop)
            break;
    }
    printf("  selfrender %s %s bars: %d notes, notes-hash %016llx\n", tpl, bars, self_notes,
           (unsigned long long)self_hash);
}

/* ---- the test's hook at each window's start (runtime.c): muteat's mutes, selfscript's actions */
#define MUTES 64
static struct {
    bl_tick_t tick;
    uint8_t vid, on, done;
} mute_at[MUTES];
static int nmute_at;
static bl_script_t ss;                   /* selfscript's */
static int ss_on, ss_next;

static int ss_supported(const bl_sact_t *a)   /* the actions that are one engine action each, and load */
{
    switch (a->kind) {
    case BL_SA_TRANSPOSE: case BL_SA_REGENERATE: case BL_SA_BLANK: case BL_SA_LOCK: case BL_SA_KEY:
    case BL_SA_DENSITY: case BL_SA_BPART: case BL_SA_VOICE: case BL_SA_ARRANGER: case BL_SA_ADVANCE: case BL_SA_LOAD:
        return 1;
    }
    return 0;
}

static void ss_apply(const bl_sact_t *s)  /* as script.c's scr_apply makes them */
{
    static bl_project_t p;
    static const uint8_t KIND[] = {
        [BL_SA_TRANSPOSE] = BL_A_TRANSPOSE, [BL_SA_REGENERATE] = BL_A_REGENERATE, [BL_SA_BLANK] = BL_A_BLANK,
        [BL_SA_LOCK] = BL_A_LOCK, [BL_SA_KEY] = BL_A_KEY, [BL_SA_DENSITY] = BL_A_DENSITY, [BL_SA_BPART] = BL_A_BPART,
        [BL_SA_VOICE] = BL_A_VOICE_ACTIVE, [BL_SA_ARRANGER] = BL_A_ARRANGER, [BL_SA_ADVANCE] = BL_A_ADVANCE,
    };
    bl_action_t a;
    if (s->kind == BL_SA_LOAD) {         /* AppServices.applyTemplate: the template, the seed; from its bar line */
        bl_template(s->tpl, &p);
        p.seed = s->seed;
        rt_load(&p);
        return;
    }
    memset(&a, 0, sizeof a);
    a.kind = KIND[s->kind];
    a.voice = s->voice;
    a.arg = s->a;
    if (s->kind == BL_SA_KEY) {
        a.key.root = s->a;
        bl_scale_preset(s->b, &a.key.scale);
    } else if (s->kind == BL_SA_DENSITY) {
        a.value = scr_density_of(s->a);
    }
    if (rt_action(&a))
        printf("FAIL selfscript: tick %lld: the engine's queue is full\n", (long long)rt.head);
}

static bl_tick_t host_window(bl_tick_t head)
{
    bl_tick_t lim = INT64_MAX;
    int i;
    for (i = 0; i < nmute_at; i++)
        if (!mute_at[i].done && mute_at[i].tick <= head) {
            uint8_t bit = (uint8_t)(1u << mute_at[i].vid);
            rt.muted = (uint8_t)(mute_at[i].on ? rt.muted | bit : rt.muted & ~bit);
            mute_at[i].done = 1;
        }
    if (!ss_on)
        return lim;
    while (ss_next < ss.nact && ss.act[ss_next].tick <= head)
        ss_apply(&ss.act[ss_next++]);
    lim = (bl_tick_t)ss.bars * BL_TICKS_PER_BAR;
    if (ss_next < ss.nact && ss.act[ss_next].tick < lim)
        lim = ss.act[ss_next].tick;
    return lim;
}

static int muteat(const char *voice, bl_tick_t tick, int on)
{
    int v = bl_vid(voice);
    if (v == BL_VID_NONE || nmute_at == MUTES)
        return -1;
    mute_at[nmute_at].tick = tick;
    mute_at[nmute_at].vid = (uint8_t)v;
    mute_at[nmute_at].on = (uint8_t)!!on;
    mute_at[nmute_at++].done = 0;
    rt_host_window = host_window;
    return 0;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf)
        buf[n] = 0;
    fclose(f);
    return buf;
}

static void selfscript(const char *path)
{
    static bl_project_t p;
    char err[200], *text = read_file(path);
    int i;
    if (rt_playing() || !text) {
        printf("FAIL selfscript %s: playing, or no such file\n", path);
        free(text);
        return;
    }
    bl_script_free(&ss);
    ss_on = 0;
    if (bl_script_parse(text, &ss, err, sizeof err)) {
        printf("FAIL selfscript %s: %s\n", path, err);
        free(text);
        return;
    }
    free(text);
    for (i = 0; i < ss.nact; i++)
        if (!ss_supported(&ss.act[i])) {
            printf("FAIL selfscript %s: action %d (at %lld) is not one engine action\n", path, i + 1,
                   (long long)ss.act[i].tick);
            return;
        }
    bl_script_project(&ss, &p);
    rt_load(&p);
    proj_unseeded = 0;                   /* (the transport's project is the test's: Play draws no seed over it) */
    for (i = 0; i < ss.nmute; i++)
        if (muteat(BL_VID_NAME[ss.mute[i].voice], ss.mute[i].tick, ss.mute[i].on)) {
            printf("FAIL selfscript %s: more than %d mutes\n", path, MUTES);
            return;
        }
    ss_on = 1;
    ss_next = 0;
    rt_host_window = host_window;
    printf("  selfscript %s: %d actions, %d mutes, %d bars\n", path, ss.nact, ss.nmute, (int)ss.bars);
}

/* ---- the MIDI dump */
static FILE *dump;
static char dump_path[256];
static int midi_cmp = -1, midi_lines, dump_count, dump_bad, per_ms;

static void host_sent(bl_tick_t tick, uint32_t pkt)
{
    static uint32_t ms, n;
    if (plat_ms() != ms)
        ms = plat_ms(), n = 0;
    if (++n > (uint32_t)per_ms)
        per_ms = (int)n;
    if (dump)
        fprintf(dump, "%lld %02X %02X %02X\n", (long long)tick, (unsigned)(pkt >> 8) & 0xFFu,
                (unsigned)(pkt >> 16) & 0xFFu, (unsigned)(pkt >> 24));
}

/* Host-only diagnostics: exact line LCS using Hirschberg's linear-space split.
 * Timestamps and wire bytes remain part of equality, and order is never sorted.
 * Repeated identical events can have ambiguous alignments; ties choose the first
 * split/match. Unmatched runs are reported completely, with order-paired changes
 * distinguished from remaining extra/missing events, not asserted note identity.
 * O(actual * reference) time, O(actual + reference) storage; explicit errors
 * mark incomplete analysis instead of silently imposing a search/output cap. */
typedef struct { const char *text; size_t len; } midi_diag_line_t;
typedef struct {
    char *data;
    midi_diag_line_t *line;
    size_t n;
} midi_diag_stream_t;
typedef struct {
    midi_diag_stream_t actual, reference;
    size_t ai, bi, matched, changed, extra, missing, hunks;
    size_t tick_changes, status_changes, data1_changes, data2_changes, formatting, unparsed;
} midi_diag_t;

static int midi_diag_read(const char *path, midi_diag_stream_t *s)
{
    FILE *f = path && *path ? fopen(path, "rb") : NULL;
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return -1; }
    long bytes = ftell(f);
    if (bytes < 0 || (uintmax_t)bytes >= (uintmax_t)SIZE_MAX || fseek(f, 0, SEEK_SET)) {
        fclose(f); return -1;
    }
    size_t n = (size_t)bytes;
    s->data = malloc(n + 1);
    if (!s->data) { fclose(f); return -1; }
    size_t got = fread(s->data, 1, n, f);
    int bad = ferror(f);
    fclose(f);
    if (got != n || bad) return -1;
    s->data[n] = 0;
    for (size_t i = 0; i < n; i++) s->n += s->data[i] == '\n';
    if (n && s->data[n - 1] != '\n') s->n++;
    if (s->n > SIZE_MAX / sizeof *s->line) return -1;
    s->line = s->n ? malloc(s->n * sizeof *s->line) : NULL;
    if (s->n && !s->line) return -1;
    size_t start = 0, k = 0;
    for (size_t i = 0; i < n; i++) if (s->data[i] == '\n') {
        s->line[k++] = (midi_diag_line_t){s->data + start, i + 1 - start};
        start = i + 1;
    }
    if (start < n) s->line[k] = (midi_diag_line_t){s->data + start, n - start};
    return 0;
}

static int midi_diag_equal(const midi_diag_t *d, size_t a, size_t b)
{
    const midi_diag_line_t *x = &d->actual.line[a], *y = &d->reference.line[b];
    return x->len == y->len && !memcmp(x->text, y->text, x->len);
}

static void midi_diag_event(const char *label, const midi_diag_stream_t *s, size_t i)
{
    fprintf(stderr, "  %s[%zu] ", label, i + 1);
    const midi_diag_line_t *l = &s->line[i];
    for (size_t j = 0; j < l->len; j++) {
        unsigned char c = (unsigned char)l->text[j];
        if (c >= 32 && c < 127 && c != '\\') fputc(c, stderr);
        else fprintf(stderr, "\\x%02X", (unsigned)c);
    }
    fputc('\n', stderr); /* escaped terminators make missing final newlines visible */
}

static int midi_diag_fields(const midi_diag_line_t *l, long long *tick, unsigned bytes[3])
{
    char text[96], time[24], wire[3][16], extra, *end;
    if (l->len >= sizeof text || memchr(l->text, 0, l->len)) return 0;
    memcpy(text, l->text, l->len);
    text[l->len] = 0;
    if (sscanf(text, "%23s %15s %15s %15s %c", time, wire[0], wire[1], wire[2], &extra) != 4) return 0;
    errno = 0;
    *tick = strtoll(time, &end, 10);
    if (errno || *end) return 0;
    for (int i = 0; i < 3; i++) {
        errno = 0;
        unsigned long value = strtoul(wire[i], &end, 16);
        if (errno || *end || value > 255) return 0;
        bytes[i] = (unsigned)value;
    }
    return 1;
}

static void midi_diag_gap(midi_diag_t *d, size_t a, size_t b)
{
    size_t na = a - d->ai, nb = b - d->bi, pairs = na < nb ? na : nb;
    if (!na && !nb) return;
    d->hunks++;
    fprintf(stderr, "MIDI-DIAG hunk=%zu actual_start=%zu count=%zu reference_start=%zu count=%zu\n",
            d->hunks, d->ai + 1, na, d->bi + 1, nb);
    if (d->ai && d->bi) {
        midi_diag_event("previous actual anchor", &d->actual, d->ai - 1);
        midi_diag_event("previous reference anchor", &d->reference, d->bi - 1);
    }
    for (size_t j = 0; j < pairs; j++) {
        long long ta, tb;
        unsigned va[3], vb[3];
        fprintf(stderr, "  changed (paired in stream order)\n");
        midi_diag_event("actual", &d->actual, d->ai + j);
        midi_diag_event("reference", &d->reference, d->bi + j);
        if (midi_diag_fields(&d->actual.line[d->ai + j], &ta, va) &&
            midi_diag_fields(&d->reference.line[d->bi + j], &tb, vb)) {
            d->tick_changes += ta != tb;
            d->status_changes += va[0] != vb[0];
            d->data1_changes += va[1] != vb[1];
            d->data2_changes += va[2] != vb[2];
            d->formatting += ta == tb && !memcmp(va, vb, sizeof va);
        } else d->unparsed++;
    }
    for (size_t j = pairs; j < na; j++) midi_diag_event("extra actual", &d->actual, d->ai + j);
    for (size_t j = pairs; j < nb; j++) midi_diag_event("missing reference", &d->reference, d->bi + j);
    if (a < d->actual.n && b < d->reference.n) {
        midi_diag_event("next actual anchor", &d->actual, a);
        midi_diag_event("next reference anchor", &d->reference, b);
    }
    d->changed += pairs;
    d->extra += na - pairs;
    d->missing += nb - pairs;
}

/* Prefix LCS lengths; reverse computes suffix lengths without reversing events. */
static size_t *midi_diag_row(const midi_diag_t *d, size_t a0, size_t a1, size_t b0, size_t b1, int reverse)
{
    size_t n = b1 - b0;
    if (n >= SIZE_MAX / sizeof(size_t)) return NULL;
    size_t *row = calloc(n + 1, sizeof *row);
    if (!row) return NULL;
    for (size_t i = 0; i < a1 - a0; i++) {
        size_t diagonal = 0;
        for (size_t j = 1; j <= n; j++) {
            size_t old = row[j];
            if (midi_diag_equal(d, reverse ? a1 - i - 1 : a0 + i, reverse ? b1 - j : b0 + j - 1))
                row[j] = diagonal + 1;
            else if (row[j - 1] > row[j]) row[j] = row[j - 1];
            diagonal = old;
        }
    }
    return row;
}

static int midi_diag_align(midi_diag_t *d, size_t a0, size_t a1, size_t b0, size_t b1)
{
    if (a0 == a1 || b0 == b1) return 0;
    if (a1 - a0 == 1) {
        for (size_t b = b0; b < b1; b++) if (midi_diag_equal(d, a0, b)) {
            midi_diag_gap(d, a0, b);
            d->ai = a0 + 1;
            d->bi = b + 1;
            d->matched++;
            break;
        }
        return 0;
    }
    size_t mid = a0 + (a1 - a0) / 2, split = 0, best = 0, n = b1 - b0;
    size_t *left = midi_diag_row(d, a0, mid, b0, b1, 0);
    size_t *right = midi_diag_row(d, mid, a1, b0, b1, 1);
    if (!left || !right) { free(left); free(right); return -1; }
    for (size_t j = 0; j <= n; j++) if (left[j] + right[n - j] > best) {
        best = left[j] + right[n - j];
        split = j;
    }
    free(left);
    free(right);
    if (midi_diag_align(d, a0, mid, b0, b0 + split)) return -1;
    return midi_diag_align(d, mid, a1, b0 + split, b1);
}

static void midi_diag_compare(const char *reference)
{
    int saved_errno = errno;
    midi_diag_t d = {0};
    fprintf(stderr, "MIDI-DIAG actual=%s reference=%s; exact ordered alignment, all differences reported;"
            " changed pairs are alignment descriptions, not inferred note identities\n", dump_path, reference);
    if ((dump && ferror(dump)) || midi_diag_read(dump_path, &d.actual) ||
        midi_diag_read(reference, &d.reference)) {
        fprintf(stderr, "MIDI-DIAG INCOMPLETE: cannot read/allocate complete streams; no completeness claim\n");
    } else if (midi_diag_align(&d, 0, d.actual.n, 0, d.reference.n)) {
        fprintf(stderr, "MIDI-DIAG INCOMPLETE: alignment allocation failed; preceding hunks are partial only\n");
    } else {
        midi_diag_gap(&d, d.actual.n, d.reference.n);
        fprintf(stderr, "MIDI-DIAG COMPLETE actual=%zu reference=%zu exact_matches=%zu changed_pairs=%zu"
                " extra_actual=%zu missing_reference=%zu hunks=%zu\n", d.actual.n, d.reference.n,
                d.matched, d.changed, d.extra, d.missing, d.hunks);
        fprintf(stderr, "MIDI-DIAG changed-pair fields (overlapping counts): tick=%zu status/channel=%zu"
                " data1=%zu data2=%zu formatting_only=%zu unparsed=%zu;"
                " accounted_actual=%zu accounted_reference=%zu\n", d.tick_changes, d.status_changes,
                d.data1_changes, d.data2_changes, d.formatting, d.unparsed,
                d.matched + d.changed + d.extra, d.matched + d.changed + d.missing);
    }
    free(d.actual.line); free(d.actual.data);
    free(d.reference.line); free(d.reference.data);
    errno = saved_errno;
}

static void cmpmidi(const char *path)
{
    FILE *a, *b;
    char la[64], lb[64];
    int n = 0;
    midi_cmp = -1;
    midi_lines = 0;
    if (dump)
        fflush(dump);
    a = dump_path[0] ? fopen(dump_path, "r") : NULL;
    b = fopen(path, "r");
    if (!a || !b) {
        printf("FAIL cmpmidi %s: no dump, or no such file\n", path);
    } else {
        for (;;) {
            char *ga = fgets(la, sizeof la, a), *gb = fgets(lb, sizeof lb, b);
            if (!ga && !gb) {
                midi_cmp = 0;
                break;
            }
            n++;
            if (!ga || !gb || strcmp(la, lb)) {
                midi_cmp = n;
                printf("FAIL cmpmidi %s: line %d: sent %s, the reference %s\n", path, n,
                       ga ? strtok(la, "\n") : "(nothing)", gb ? strtok(lb, "\n") : "(its end)");
                break;
            }
        }
        midi_lines = midi_cmp ? n - 1 : n;
        if (!midi_cmp)
            printf("  cmpmidi %s: %d lines, the same\n", path, n);
    }
    if (a)
        fclose(a);
    if (b)
        fclose(b);
    const char *diag = getenv("TANGRAM_MIDI_DIAG");
    if (diag && !strcmp(diag, "1")) midi_diag_compare(path);
}

/* the dump's lines with st d1 d2 (each -1: any; at tick, unless it is -1): how many; the k-th from the end (k > 0) in
 * *kth */
static int dump_scan(long st, long d1, long d2, long long tick, int k, char *kth, int max)
{
    FILE *f;
    char line[64];
    static char ring[64][64];
    int n = 0, total = 0;
    long long t;
    unsigned a, b, c;
    if (dump)
        fflush(dump);
    f = dump_path[0] ? fopen(dump_path, "r") : NULL;
    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        snprintf(ring[total++ % 64], 64, "%s", strtok(line, "\n") ? line : "");
        if (sscanf(line, "%lld %x %x %x", &t, &a, &b, &c) == 4 && (st < 0 || a == (unsigned)st)
            && (d1 < 0 || b == (unsigned)d1) && (d2 < 0 || c == (unsigned)d2) && (tick < 0 || t == tick))
            n++;
    }
    fclose(f);
    if (k > 0)
        snprintf(kth, (size_t)max, "%s", k <= total && k <= 64 ? ring[(total - k) % 64] : "(none)");
    return n;
}

static long dump_byte(const char *w) { return !strcmp(w, "*") ? -1 : (long)strtoul(w, 0, 16); }   /* "*": any */

/* racefc: the next start meets an FC from the tick (the interrupt) between app_loop's look at an FA and the start's
 * lock */
static void race_fc(void)
{
    rt_host_start = 0;
    min_q[mi_w % MQ] = 0x0Fu | 0xFCu << 8;
    min_port[mi_w++ % MQ] = PORT_TRS;
    app_tick();
}

/* ---- the transport's controls */
static int rt_cmd(const char *a, const char *b, const char *c)
{
    static bl_project_t p;
    if (!strcmp(a, "play"))
        rt_play();
    else if (!strcmp(a, "stop"))
        rt_stop();
    else if (!strcmp(a, "tempo"))
        rt_set_tempo(atoi(b));
    else if (!strcmp(a, "load") && tpl_of(b) < BL_NTPL) {
        bl_template(tpl_of(b), &p);
        if (c[0])
            p.seed = strtoull(c, 0, 0);
        rt_load(&p);
        proj_unseeded = 0;               /* (the transport's project is the test's: Play draws no seed over it) */
    } else if (!strcmp(a, "mute") && bl_vid(b) != BL_VID_NONE)
        rt_set_mute(bl_vid(b), atoi(c));
    else if (!strcmp(a, "channel") && bl_vid(b) != BL_VID_NONE)
        rt_set_channel(bl_vid(b), (uint8_t)atoi(c));
    else if (!strcmp(a, "sync") && (!strcmp(b, "int") || !strcmp(b, "ext"))) {
        device_settings_t settings = device_settings_get();
        settings.sync = b[0] == 'e' ? DEVICE_SYNC_EXTERNAL : DEVICE_SYNC_INTERNAL;
        return rt_playing() ? -1 : device_settings_set(settings);
    } else if (!strcmp(a, "port") &&
               (!strcmp(b, "USB") || !strcmp(b, "TRS") || !strcmp(b, "BLE") ||
                !strcmp(b, "both") || !strcmp(b, "usbble") || !strcmp(b, "trsble") || !strcmp(b, "all"))) {
        device_settings_t settings = device_settings_get();
        settings.ext_ports = !strcmp(b, "USB") ? DEVICE_CLOCK_USB :
            !strcmp(b, "TRS") ? DEVICE_CLOCK_TRS : !strcmp(b, "BLE") ? DEVICE_CLOCK_BLE :
            !strcmp(b, "usbble") ? DEVICE_CLOCK_USB | DEVICE_CLOCK_BLE :
            !strcmp(b, "trsble") ? DEVICE_CLOCK_TRS | DEVICE_CLOCK_BLE :
            !strcmp(b, "all") ? DEVICE_CLOCK_ALL : DEVICE_CLOCK_BOTH;
        return device_settings_set(settings);
    } else if ((!strcmp(a, "midiout") || !strcmp(a, "clockout")) &&
               (!strcmp(b, "USB") || !strcmp(b, "BLE") || !strcmp(b, "both"))) {
        device_settings_t settings = device_settings_get();
        uint8_t target = !strcmp(b, "USB") ? DEVICE_MIDI_USB :
                         !strcmp(b, "BLE") ? DEVICE_MIDI_BLE : DEVICE_MIDI_BOTH;
        if (!strcmp(a, "midiout")) settings.midi_out = target;
        else settings.clock_out = target;
        return device_settings_set(settings);
    }
    else if (!strcmp(a, "burst") && rt_playing() && atoi(b) > 0 && atoi(b) <= 128) {
        uint32_t f = plat_lock(), n;
        bl_tick_t t = rt.started ? rt.tick : 0;
        for (n = 0; n < (uint32_t)atoi(b); n++)
            /* Match the already queued off's absolute tick for stale-off protection. */
            rt_insert(t, rt_pkt(0x9Fu, n, 1), BL_VID_NONE)->end = t + 1;
        for (n = 0; n < (uint32_t)atoi(b); n++)
            rt_insert(t + 1, rt_pkt(0x8Fu, n, 0), BL_VID_NONE);
        plat_unlock(f);
    } else
        return -2;
    return 0;
}

/* ---- the project's changes, the slots, undo */
static int voice_of(const char *id)      /* the project template's voice of that id, -1 none */
{
    static bl_project_t p;
    int i;
    bl_template(proj.tpl, &p);
    for (i = 0; i < p.nvoices; i++)
        if (!strcmp(p.voice[i].id, id))
            return i;
    return -1;
}

static int form_named(const char *w)    /* a FORM_ preset, FORM_OTHER for none */
{
    static const char *const NAME[] = {"", "classic", "slow", "ambient"};
    int f = FORM_CLASSIC;
    while (f <= FORM_AMBIENT && strcmp(NAME[f], w))
        f++;
    return f <= FORM_AMBIENT ? f : FORM_OTHER;
}

static int on_off(const char *w) { return !strcmp(w, "on") ? 1 : !strcmp(w, "off") ? 0 : -1; }

static int mode_named(const char *w)
{
    int m = 0;
    while (m < BL_NMODES && strcmp(BL_MODE_RAW[m], w))
        m++;
    return m;
}

static int proj_cmd(const char *a, const char *b, const char *c)
{
    int r = 0;
    if (!strcmp(a, "template") && tpl_of(b) < BL_NTPL)
        r = c[0] ? template_seeded(tpl_of(b), strtoull(c, 0, 0)) : proj_set_template(tpl_of(b));
    else if (!strcmp(a, "key") && mode_named(c) < BL_NMODES)
        r = proj_set_key(atoi(b), mode_named(c));
    else if (!strcmp(a, "tempo"))
        proj_set_bpm(atoi(b));
    else if (!strcmp(a, "density"))
        r = proj_set_density(atoi(b));
    else if (!strcmp(a, "arranger") && on_off(b) >= 0)
        r = proj_set_arranger(on_off(b));
    else if (!strcmp(a, "form") && form_named(b) != FORM_OTHER)
        r = proj_set_form(form_named(b));
    else if (!strcmp(a, "ambient") && on_off(b) >= 0)
        r = proj_set_ambient(on_off(b));
    else if (!strcmp(a, "channel") && voice_of(b) >= 0)
        proj_set_channel(voice_of(b), atoi(c));
    else if (!strcmp(a, "sync") && (!strcmp(b, "int") || !strcmp(b, "ext")))
        r = proj_set_sync(b[0] == 'e');
    else if (!strcmp(a, "ratchet")) {
        /* Strict decimal input, bounded before any possible integer overflow.
         * Reject before calling the setter so malformed commands cannot edit state. */
        int pct = 0;
        if (!b[0] || c[0]) return -1;
        for (const char *p = b; *p; p++) {
            if (*p < '0' || *p > '9') return -1;
            pct = pct * 10 + (*p - '0');
            if (pct > 50) return -1;
        }
        r = proj_set_ratchet(pct); /* -1 invalid, 0 accepted, 1 accepted with deferred ratchet difference */
    }
    else if (!strcmp(a, "dice"))
        r = proj_dice();
    else if (!strcmp(a, "format1")) {
        proj_t rec = proj;
        rec.format = 1;
        rec.ratchet_pct = 35;
        r = plat_store_save(OBJ_PROJ, &rec, sizeof rec);
    }
    else
        return -2;
    return r;
}

static uint64_t pat_hash(const bl_state_t *s)
{
    char line[128];
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    int i, k;
    for (i = 0; i < s->n; i++) {
        const bl_pattern_t *p = &s->v[i].pat;
        snprintf(line, sizeof line, "%s %lld %d\n", s->v[i].id, (long long)p->length, p->n);
        h = fnv(h, line);
        for (k = 0; k < p->n; k++) {
            const bl_pnote_t *q = &p->note[k];
            snprintf(line, sizeof line, "%lld %lld %d %d %d %d %d %d\n", (long long)q->position, (long long)q->gate,
                     (int)q->pitch.degree, (int)q->pitch.octave, (int)q->pitch.accidental, q->velocity, q->accent,
                     q->ratchet);
            h = fnv(h, line);
        }
    }
    return h;
}

/* pat_hash_hi / _lo, slot_patK_hi / _lo: 1 and the half in *got, or 0 for another name (or an empty slot: -1) */
static int pat_expect(const char *what, int *got)
{
    static bl_state_t s;
    const char *half;
    int i, k;
    if (!strcmp(what, "ratchets")) {
        rt_capture(&s);
        for (*got = 0, i = 0; i < s.n; i++)
            for (k = 0; k < s.v[i].pat.n; k++)
                *got += s.v[i].pat.note[k].ratchet > 1;
        return 1;
    }
    if (!strncmp(what, "pat_hash_", 9)) {
        rt_capture(&s);
        half = what + 9;
    } else if (!strncmp(what, "slot_pat", 8) && what[8] >= '0' && what[8] < '0' + PROJ_SLOTS && what[9] == '_') {
        k = what[8] - '0';
        if (!slot_used(k) || state_unpack(slots[k].rec + SLOT_HEAD, slots[k].n - SLOT_HEAD, &s)) {
            *got = -1;
            return 1;
        }
        half = what + 10;
    } else {
        return 0;
    }
    if (strcmp(half, "hi") && strcmp(half, "lo"))
        return 0;
    *got = (int)(uint32_t)(half[0] == 'h' ? pat_hash(&s) >> 32 : pat_hash(&s));
    return 1;
}

static int proj_expect(const char *what, int *got)
{
    if (!strcmp(what, "tpl"))
        *got = proj.tpl;
    else if (!strcmp(what, "root"))
        *got = proj.key_root;
    else if (!strcmp(what, "mode"))
        *got = proj.key_mode;
    else if (!strcmp(what, "bpm_proj"))
        *got = proj.bpm;
    else if (!strcmp(what, "seed_hi"))
        *got = (int)(uint32_t)(proj.seed >> 32);
    else if (!strcmp(what, "seed_lo"))
        *got = (int)(uint32_t)proj.seed;
    else if (!strncmp(what, "chan", 4) && what[4] >= '0' && what[4] < '0' + BL_MAX_VOICES && !what[5])
        *got = proj.channel[what[4] - '0'];
    else if (!strcmp(what, "density_pct"))
        *got = proj.density_pct;
    else if (!strcmp(what, "has_density"))
        *got = proj.has_density;
    else if (!strcmp(what, "form"))
        *got = proj_form();
    else if (!strcmp(what, "ux_global_pages"))
        *got = VIEW_PAGES[V_SETUP];
    else if (!strcmp(what, "ux_global_help_cards"))
        *got = help_count(B_GLO);
    else if (!strcmp(what, "ux_env_pages"))
        *got = VIEW_PAGES[V_VOICE];
    else if (!strcmp(what, "ux_env_is_voice"))
        *got = ui.view == V_VOICE && !strcmp(view_name(V_VOICE), "ENV VOICE");
    else if (!strcmp(what, "ux_env_timing_unchanged"))
        *got = VIEW_KNOB[V_VOICE][0] == KN_RATE && VIEW_KNOB[V_VOICE][1] == KN_GATE &&
               VIEW_KNOB[V_VOICE][2] == KN_LOOP && VIEW_KNOB[V_VOICE][3] == KN_PHASE;
    else if (!strcmp(what, "ux_env_voice_unchanged"))
        *got = VOICE_KNOB[0] == KN_CHANNEL && VOICE_KNOB[1] == KN_REGISTER &&
               VOICE_KNOB[2] == KN_VDENSITY && VOICE_KNOB[3] == KN_COMP_TENSION;
    else if (!strcmp(what, "ux_fx_pages"))
        *got = VIEW_PAGES[V_DELAY];
    else if (!strcmp(what, "ux_fx_is_mixer"))
        *got = ui.view == V_DELAY && !strcmp(view_name(V_DELAY), "FX MIXER");
    else if (!strcmp(what, "ux_lfo_target") ||
             !strcmp(what, "ux_lfo_cycle") ||
             !strcmp(what, "ux_lfo_depth") ||
             !strcmp(what, "ux_lfo_center")) {
        int target, cycle, depth, center;
        if (proj_lfo_get(key_voice(ui.sel), &target, &cycle, &depth, &center)) return 0;
        *got = !strcmp(what, "ux_lfo_target") ? target :
               !strcmp(what, "ux_lfo_cycle") ? cycle :
               !strcmp(what, "ux_lfo_depth") ? depth : center;
    }
    else if (!strcmp(what, "ux_mixer_volume") ||
             !strcmp(what, "ux_mixer_pan") ||
             !strcmp(what, "ux_mixer_cutoff") ||
             !strcmp(what, "ux_mixer_resonance")) {
        int control = !strcmp(what, "ux_mixer_volume") ? MIDI_MIX_VOLUME :
                      !strcmp(what, "ux_mixer_pan") ? MIDI_MIX_PAN :
                      !strcmp(what, "ux_mixer_cutoff") ? MIDI_MIX_CUTOFF : MIDI_MIX_RESONANCE;
        *got = proj_mixer_value(key_voice(ui.sel), control);
    }
    else if (!strcmp(what, "ux_project_record_bytes"))
        *got = PROJECT_RECORD_BYTES;
    else if (!strcmp(what, "ux_bank_bytes"))
        *got = PERFORMANCE_BANK_BYTES;
    else if (!strcmp(what, "ux_bank_slot1"))
        *got = performance_preset_used(1);
    else if (!strcmp(what, "ux_bank_flash_bytes")) {
        static uint8_t probe[PLAT_STORE_MAX];
        *got = plat_store_load(OBJ_PERFORMANCE_PRESETS, probe, sizeof probe);
    }
    else if (!strcmp(what, "ux_pending_cells")) {
        *got = 0;
        for (int i = 0; i < 4; i++) {
            cell_t c;
            int k = view_knob(ui.view, i);
            cell(ui.view, i, &c);
            *got += cell_active_value(k, &c) != c.v;
        }
    }
    else if (!strcmp(what, "composer_section"))
        *got = rt_engine()->composer.section;
    else if (!strcmp(what, "composer_pending_form"))
        *got = rt_engine()->composer.pending_form;
    else if (!strcmp(what, "composer_active_form_min"))
        *got = rt_engine()->project.arr.min_bars;
    else if (!strcmp(what, "composer_active_unit"))
        *got = rt_engine()->composer.unit;
    else if (!strcmp(what, "arr_has"))
        *got = proj.arr_has;
    else if (!strcmp(what, "arr_on"))
        *got = proj.arr_on;
    else if (!strcmp(what, "arr_ambient"))
        *got = proj.arr_ambient;
    else if (!strcmp(what, "ratchet"))
        *got = proj.ratchet_pct;
    else if (!strcmp(what, "save_pending"))
        *got = proj_save_pending();
    else if (!strcmp(what, "save_unsent"))
        *got = save_unsent;
    else if (!strcmp(what, "seeded"))
        *got = !proj_unseeded;
    else if (!strcmp(what, "sync"))
        *got = device_settings_get().sync;
    else if (!strcmp(what, "ext_ports"))
        *got = device_settings_get().ext_ports;
    else if (!strcmp(what, "midi_out"))
        *got = device_settings_get().midi_out;
    else if (!strcmp(what, "clock_out"))
        *got = device_settings_get().clock_out;
    else if (!strcmp(what, "midi_sent_usb"))
        *got = (int)plat_midi_count(PORT_USB, 1);   /* realtime clock is excluded */
    else if (!strcmp(what, "midi_sent_ble"))
        *got = (int)plat_midi_count(PORT_BLE, 1);   /* realtime clock is excluded */
    else if (!strcmp(what, "lights"))
        *got = device_settings_get().lights;
    else if (!strncmp(what, "slot_used", 9) && what[9] >= '0' && what[9] < '0' + PROJ_SLOTS && !what[10])
        *got = slot_used(what[9] - '0');
    else if (!strncmp(what, "slot_lit", 8) && what[8] >= '0' && what[8] < '0' + PROJ_SLOTS && !what[9])
        *got = (int)(ui.led_k >> (SLOT_KEY0 + what[8] - '0') & 1u);
    else if (!strcmp(what, "save_lit"))
        *got = (int)(ui.led_b >> B_SAVE & 1u);
    else if (!strcmp(what, "undo_depth"))
        *got = undo_n;
    else if (!strcmp(what, "stale"))
        *got = proj_stale;
    else
        return pat_expect(what, got);
    return 1;
}

/* a check of voice key V (0-6, role order) or key K (0-26): its number in *n, else 0 */
static int indexed(const char *what, const char *name, int max, int *n)
{
    size_t l = strlen(name);
    char *end;
    long v;
    if (strncmp(what, name, l) || what[l] < '0' || what[l] > '9')
        return 0;
    v = strtol(what + l, &end, 10);
    if (*end || v >= max)
        return 0;
    *n = (int)v;
    return 1;
}

/* the convergence as the Perform page shows it ("1680 bars ~ 4200 s", "0.9 bars ~ 2 s"), read back: the bars in
 * tenths and the seconds (-1 both: INF). A text of any other shape is a FAIL line, and INT32_MIN for both (no expect
 * holds) */
static void lcm_shown(int64_t period, int *tenths, int *secs)
{
    char t[40], *e, *b;
    long w, f = 0, sec;
    convergence_text(period, t, 0);
    if (!strcmp(t, "INF (practically)")) {
        *tenths = *secs = -1;
        return;
    }
    w = strtol(t, &e, 10);
    if (e != t && *e == '.' && e[1] >= '0' && e[1] <= '9' && !(e[2] >= '0' && e[2] <= '9'))
        f = strtol(e + 1, &e, 10);
    b = e;
    if (e != t && (!strncmp(b, " bars ~ ", 8) || !strncmp(b, " bar ~ ", 7))) {
        b = strchr(b, '~') + 2;
        sec = strtol(b, &e, 10);
        if (e != b && !strcmp(e, " s")) {
            *tenths = (int)(w * 10 + f);
            *secs = (int)sec;
            return;
        }
    }
    printf("FAIL the convergence's text \"%s\" (period %lld): not \"N[.T] bars ~ S s\" nor INF\n", t, (long long)period);
    *tenths = *secs = INT32_MIN;
}

static int ui_expect(const char *what, int *got)   /* the panel's (ui.c), the mirror refreshed */
{
    int v;
    mirror();
    if (!strcmp(what, "view"))
        *got = ui.view;
    else if (!strcmp(what, "voice_sel"))
        *got = ui.sel;
    else if (!strcmp(what, "degree"))
        *got = degree_now();
    else if (!strcmp(what, "pending_degree"))
        *got = degree_pending();
    else if (!strcmp(what, "tension_left"))
        *got = tension_left();
    else if (!strcmp(what, "arr_phase"))
        *got = ui.st_ok && ui_st.has_arr ? ui_st.phase : -1;
    else if (!strcmp(what, "warns"))
        *got = (int)ui.warns;
    else if (!strcmp(what, "help"))
        *got = ui.help;
    else if (indexed(what, "muted", NVKEYS, &v))
        *got = key_vid(v) != BL_VID_NONE && rt_muted(key_vid(v));
    else if (indexed(what, "active", NVKEYS, &v))
        *got = v_active(v);
    else if (indexed(what, "locked", NVKEYS, &v))
        *got = v_locked(v);
    else if (indexed(what, "gen", NVKEYS, &v))
        *got = vstat(v) < 0 ? -1 : (int)ui_st.voice[vstat(v)].generation;
    else if (indexed(what, "req", NVKEYS, &v))
        *got = vstat(v) < 0 ? -1 : ui_st.voice[vstat(v)].pending;
    else if (indexed(what, "blank", NVKEYS, &v))
        *got = vstat(v) < 0 ? -1 : ui_st.voice[vstat(v)].blank;
    else if (indexed(what, "key_led", NKEYS, &v))
        *got = ui.led_k >> v & 1u ? 2 : (int)(ui.dim_k >> v & 1u);
    else if (indexed(what, "btn_led", NB, &v))
        *got = (int)(ui.led_b >> v & 1u);
    else if (!strcmp(what, "lanes"))
        *got = (lanes(), nlanes);
    else if (!strncmp(what, "lcm_tenths", 10) || !strncmp(what, "lcm_secs", 8)) {   /* the convergence as shown */
        int t, sec;
        lanes();
        lcm_shown(what[4] == 't' ? (what[10] ? strtoll(what + 10, 0, 10) : convergence())
                                 : (what[8] ? strtoll(what + 8, 0, 10) : convergence()), &t, &sec);
        *got = what[4] == 't' ? t : sec;
    } else if (!strcmp(what, "bpm_shown"))     /* the tempo the pages show and say */
        *got = bpm_now();
    else if (indexed(what, "delay_ms", BL_MAX_VOICES, &v))   /* the Delay page's time for the template's voice V */
        *got = v < tpl_now()->nvoices ? (int)delay_ms(delay_div(tpl_now()->voice[v].role), bpm_now()) : -1;
    else if (!strcmp(what, "delay_check")) {   /* DelayHint in integers against Swift's sums in binary64: the misses */
        int b, d;
        *got = 0;
        for (b = 40; b <= 220; b++)
            for (d = 0; d < 3; d++) {
                static const uint64_t BEATS[3] = {0x3FE8000000000000u, 0x3FE0000000000000u, 0x3FF0000000000000u};
                f64 ms = f64_mul(f64_div(F64(0x40ED4C0000000000), f64_from_i64(b)), (f64){BEATS[d]});   /* 60000.0 */
                *got += f64_to_i64(f64_round(ms)) != (int64_t)delay_ms(d, b);
            }
    } else if (!strcmp(what, "note_check")) {  /* note names (C3 = 60) and registers as shown and said: the misses */
        static const struct {
            uint8_t n;
            const char *name;
        } N[] = {{0, "C-2"}, {11, "B-2"}, {12, "C-1"}, {23, "B-1"}, {24, "C0"}, {48, "C2"}, {55, "G2"}, {60, "C3"},
                 {72, "C4"}, {79, "G4"}, {127, "G8"}};
        char t[24];
        size_t i;
        *got = 0;
        for (i = 0; i < sizeof N / sizeof N[0]; i++) {
            note_name(N[i].n, t);
            *got += strcmp(t, N[i].name) != 0;
        }
        reg_text(0, 15, t, 1);
        *got += strcmp(t, "C-2 to Eb-1") != 0;
        reg_text(55, 79, t, 0);
        *got += strcmp(t, "G2-G4") != 0;
    } else if (!strcmp(what, "deg_label_max")) {   /* the Pattern page's highest degree label (the selected voice) */
        const lane_t *l = (lanes(), sel_lane());
        bl_key_t k;
        int i;
        key_now(&k);
        *got = 0;
        for (i = 0; l && i < l->pat->n; i++)
            *got = degree_label(&l->pat->note[i], &k) > *got ? degree_label(&l->pat->note[i], &k) : *got;
    } else if (!strcmp(what, "tension_voice")) {   /* the first pending tension's voice, as its key; -1 none */
        int i, k;
        *got = -1;
        for (i = 0; ui.st_ok && i < ui_st.nup && *got < 0; i++)
            for (k = 0; ui_st.up[i].kind == BL_UP_TENSION && k < NVKEYS; k++)
                if (key_vid(k) == ui_st.up[i].voice)
                    *got = k;
    } else
        return 0;
    return 1;
}

static int rt_expect(const char *what, int *got);
int app_expect(const char *what, int *got)
{
    return ui_expect(what, got) || proj_expect(what, got) || rt_expect(what, got);   /* (the project: the main
                                                                                       * loop's, no lock) */
}

static int rt_expect(const char *what, int *got)   /* the transport's, under the lock */
{
    uint32_t f = plat_lock();
    int ok = 1, c, n;
    if (!strcmp(what, "playing"))
        *got = rt.playing;
    else if (!strcmp(what, "tick"))
        *got = (int)(rt.playing && rt.started ? rt.tick : 0);
    else if (!strcmp(what, "head"))
        *got = (int)rt.head;
    else if (!strcmp(what, "tick_rate"))
        *got = rt.rate;
    else if (!strcmp(what, "bpm"))
        *got = rt.bpm;
    else if (!strcmp(what, "tempo_tick"))
        *got = (int)rt.tempo_tick;
    else if (!strcmp(what, "queued"))
        *got = rt.qn;
    else if (!strcmp(what, "eng_ratchet")) {
        const bl_project_t *p = rt.playing ? &rt_eng.project : &rt_proj;
        *got = p->has_ratchet ? (int)f64_to_i64(f64_round(f64_mul(p->ratchet_p, HUNDRED))) : -1;
    }
    else if (!strncmp(what, "sounding", 8)) {   /* soundingC: channel C's alone (0-15); soundingC.N: its note N */
        const char *dot = strchr(what, '.');
        for (*got = 0, c = 0; c < 16; c++)
            for (n = 0; n < 128; n++)
                *got += (int)((!what[8] || c == atoi(what + 8)) && (!dot || n == atoi(dot + 1))
                              && rt.snd[c][n]);
    } else if (!strcmp(what, "per_ms"))
        *got = per_ms;
    else if (!strcmp(what, "dumpcount"))
        *got = dump_count;
    else if (!strcmp(what, "dumpbad"))
        *got = dump_bad;
    else if (!strcmp(what, "dropped"))
        *got = (int)(rt.dropped + rt.over);
    else if (!strcmp(what, "midicmp"))
        *got = midi_cmp;
    else if (!strcmp(what, "midilines"))
        *got = midi_lines;
    else if (!strcmp(what, "selfnotes"))
        *got = self_notes;
    else if (!strcmp(what, "selfhash_hi"))   /* (expect reads a hex value into an int: the same 32 bits) */
        *got = (int)(uint32_t)(self_hash >> 32);
    else if (!strcmp(what, "selfhash_lo"))
        *got = (int)(uint32_t)self_hash;
    else
        ok = 0;
    plat_unlock(f);
    return ok;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    if (!strcmp(cmd, "selfrender"))
        selfrender(a, b);
    else if (!strcmp(cmd, "selfscript"))
        selfscript(a);
    else if (!strcmp(cmd, "muteat")) {
        if (muteat(a, strtoll(b, 0, 10), atoi(c)))
            printf("FAIL muteat %s %s %s: no such voice, or too many\n", a, b, c);
    } else if (!strcmp(cmd, "dumpmidi")) {
        if (dump)
            fclose(dump);
        dump = a[0] ? fopen(a, "w") : NULL;
        snprintf(dump_path, sizeof dump_path, "%s", a);
        if (a[0] && !dump)
            printf("FAIL dumpmidi %s: cannot write it\n", a);
    } else if (!strcmp(cmd, "cmpmidi"))
        cmpmidi(a);
    else if (!strcmp(cmd, "racefc"))
        rt_host_start = race_fc;
    else if (!strcmp(cmd, "dumpcount")) {
        dump_count = dump_scan(dump_byte(a), dump_byte(b), dump_byte(c), d[0] ? strtoll(d, 0, 10) : -1, 0, NULL, 0);
        printf("  dumpcount %s %s %s%s%s: %d\n", a, b, c, d[0] ? " at " : "", d, dump_count);
    } else if (!strcmp(cmd, "dumpis")) {
        char kth[64], want[16];
        unsigned x, y, z;
        long long t;
        dump_scan(0, 0, 0, -1, atoi(a), kth, sizeof kth);
        snprintf(want, sizeof want, "%s %s %s", b, c, d);
        if (sscanf(kth, "%lld %x %x %x", &t, &x, &y, &z) == 4 && x == strtoul(b, 0, 16) && y == strtoul(c, 0, 16)
            && z == strtoul(d, 0, 16))
            printf("  ok dump[-%s] %s\n", a, kth);
        else {
            printf("FAIL dumpis %s %s: the line is %s\n", a, want, kth);
            dump_bad++;
        }
    }
    else if (!strcmp(cmd, "legacybank")) {
        /* Host-only fixture: genuine v1 bank bytes and format-8 project
         * without CC mixer/LFO extension. No device firmware path can run it. */
        static uint8_t old[PERFORMANCE_V1_BANK_BYTES], rec[PROJECT_RECORD_BYTES];
        memset(old, 0, sizeof old);
        memcpy(old, performance_legacy_head, PERFORMANCE_BANK_HEAD);
        if (project_record_write(&proj, rec, sizeof rec) != PROJECT_RECORD_BYTES) {
            printf("FAIL legacybank: cannot encode test project\n");
        } else {
            rec[4] = 8; /* first 312 bytes are identical to format 8 */
            old[PERFORMANCE_BANK_HEAD] = 1;
            memcpy(old + PERFORMANCE_BANK_HEAD + 1, "LEGACY BANK ", PERFORMANCE_PRESET_NAME_BYTES);
            memcpy(old + PERFORMANCE_BANK_HEAD + 1 + PERFORMANCE_PRESET_NAME_BYTES,
                   rec, PROJECT_RECORD8_BYTES);
            if (plat_store_save(OBJ_PERFORMANCE_PRESETS, old, sizeof old))
                printf("FAIL legacybank: flash fixture write\n");
            else printf("  ok legacy performance bank v1 seeded\n");
        }
    } else if (!strcmp(cmd, "migratebank")) {
        if (performance_preset_rename(1, "MIGRATED", 8))
            printf("FAIL migratebank: old slot cannot rename\n");
        else printf("  ok legacy bank migrated by explicit user rename\n");
    }
    else if (!strcmp(cmd, "rt")) {
        int r = rt_cmd(a, b, c);
        if (r == -2)
            printf("FAIL rt %s %s %s: no such control\n", a, b, c);
        else if (r)
            printf("  rt %s %s %s: refused (global clock edit unavailable; sync requires stopped transport)\n", a, b, c);
    } else if (!strcmp(cmd, "proj")) {
        int r = proj_cmd(a, b, c);
        if (!strcmp(a, "ratchet")) {
            if (r < 0)
                printf("  proj ratchet: refused (expected one decimal percent, 0..50 in steps of 5)\n");
            else if (r == 1)
                printf("  proj ratchet %u: accepted, deferred until next Play or template takeover\n", proj.ratchet_pct);
            else
                printf("  proj ratchet %u: accepted%s\n", proj.ratchet_pct, rt_playing() ? "" : ", applied while stopped");
        } else if (r == -2)
            printf("FAIL proj %s %s %s: no such change\n", a, b, c);
        else if (r)
            printf("  proj %s %s %s: refused (the Mac's stopped only, or the engine's queue full)\n", a, b, c);
    } else if (!strcmp(cmd, "slot") && (!strcmp(a, "store") || !strcmp(a, "recall"))) {
        if (a[0] == 's' ? slot_store(atoi(b)) : slot_recall(atoi(b)))
            printf("  slot %s %s: refused (no such slot, an empty one, or the engine's queue full)\n", a, b);
    } else if (!strcmp(cmd, "undo") && (!strcmp(a, "push") || !strcmp(a, "pop"))) {
        int r = a[1] == 'u' ? undo_push() : undo_pop();
        if (a[1] == 'u' && r)
            printf("  undo push: the state too big for a point\n");
        else if (r == UNDO_NONE)
            printf("  undo pop: nothing to undo\n");
        else if (r == UNDO_FULL)
            printf("  undo pop: the engine's queue full (the point kept)\n");
    } else if (!strcmp(cmd, "save")) {
        int r = proj_save();
        if (r < 0)
            printf("FAIL save: not written, or not read back the same\n");
        else if (r == 1)
            printf("  save: at the stop\n");
    } else
        return 0;
    return 1;
}

static void host_saving(void) { save_unsent = rt.nn; }

void app_speak_dump(void) { speak_dump(); }

void app_host_reboot(void)               /* app_init() subsequently reloads device settings from simulated flash */
{
    rt_stop();
    memset(&proj, 0, sizeof proj);
    memset(slots, 0, sizeof slots);
    undo_used = undo_n = 0;
    proj_stale = proj_unseeded = proj_saving = 0;
}
