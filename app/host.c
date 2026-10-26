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
 *      sync ext_ports lights save_pending save_lit
 *                            the app project (proj: seeded 0 while the defaults' seed is still to be drawn; chanI its
 *                            voice I's channel; form proj_form, a FORM_); a SAVE waiting for the stop; the SAVE
 *                            button's LED (blinking while one waits)
 *   slot_usedK slot_litK undo_depth stale   slot K (0-5) stored; its key lit; the undo points; a change made while
 *                            playing that the transport has not got yet (transportStale)
 *   pat_hash_hi pat_hash_lo  FNV-1a 64 over the transport's patterns (as captureState gives them: the running
 *                            engine's at the head; before a Play, the project's initial ones), their notes and lengths
 *                            only, as text: per voice "ID LENGTH N\n", then per note "POSITION GATE DEGREE OCTAVE
 *                            ACCIDENTAL VELOCITY ACCENT RATCHET\n"
 *   slot_patK_hi slot_patK_lo   the same over slot K's state
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
 *   dumpmidi [FILE]          start (FILE) or stop logging "tick status d1 d2" (hex bytes) for every message the
 *                            transport sends, in send order; a flush's messages carry the sounding tick
 *   cmpmidi FILE             the dump so far against FILE (a .midiref), line by line: midicmp, midilines
 *   dumpcount ST D1 D2 [TICK]   the dump's lines with these bytes (hex; at TICK, if given): expect dumpcount
 *   dumpis K ST D1 D2        the dump's K-th line from its end (1 the last) has these bytes; else a FAIL line and
 *                            dumpbad one more
 *   rt play | stop | tempo BPM | load TEMPLATE [SEED] | mute VOICE 0|1 | channel VOICE CH | sync int|ext |
 *      port USB|TRS|both     the transport's controls (VOICE a voice id, CH 0-15), until the panel has them all
 *   rt burst N               (playing) N note-ons on channel 16 (notes 0..N-1, velocity 1) due now and their offs a
 *                            tick later, into the queue: more than a ms may send
 *   proj template TEMPLATE [SEED] | key ROOT MODE | tempo BPM | density K | arranger on|off |
 *      form classic|slow|ambient | ambient on|off | channel VOICE CH | sync int|ext | dice
 *                            the project's changes as the Mac's AppServices makes them (project.c: MODE a ModeID raw
 *                            value, K 0-100; SEED a test's in place of the fresh one), until the panel has them all
 *   slot store K | slot recall K | undo push | undo pop | save
 *                            the slots (K 0-5), the undo stack, SAVE */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../tests/host/script.c"        /* the script parser, equiv_test's */

static int self_notes = -1;              /* the last selfrender's notes, -1 none (or it failed) */
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
}

/* the dump's lines with st d1 d2 (at tick, unless it is -1): how many; the k-th from the end (k > 0) in *kth */
static int dump_scan(uint32_t st, uint32_t d1, uint32_t d2, long long tick, int k, char *kth, int max)
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
        if (sscanf(line, "%lld %x %x %x", &t, &a, &b, &c) == 4 && a == st && b == d1 && c == d2
            && (tick < 0 || t == tick))
            n++;
    }
    fclose(f);
    if (k > 0)
        snprintf(kth, (size_t)max, "%s", k <= total && k <= 64 ? ring[(total - k) % 64] : "(none)");
    return n;
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
    else if (!strcmp(a, "sync") && (!strcmp(b, "int") || !strcmp(b, "ext")))
        rt_set_sync(b[0] == 'e');
    else if (!strcmp(a, "port") && (!strcmp(b, "USB") || !strcmp(b, "TRS") || !strcmp(b, "both")))
        rt_set_ext_port(b[0] == 'U' ? 1 << PORT_USB : b[0] == 'T' ? 1 << PORT_TRS : 1 << PORT_USB | 1 << PORT_TRS);
    else if (!strcmp(a, "burst") && rt_playing() && atoi(b) > 0 && atoi(b) <= 128) {
        uint32_t f = plat_lock(), n;
        bl_tick_t t = rt.started ? rt.tick : 0;
        for (n = 0; n < (uint32_t)atoi(b); n++)
            rt_insert(t, rt_pkt(0x9Fu, n, 1), BL_VID_NONE);
        for (n = 0; n < (uint32_t)atoi(b); n++)
            rt_insert(t + 1, rt_pkt(0x8Fu, n, 0), BL_VID_NONE);
        plat_unlock(f);
    } else
        return -1;
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
    else if (!strcmp(a, "dice"))
        r = proj_dice();
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
    int k;
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
    else if (!strcmp(what, "arr_has"))
        *got = proj.arr_has;
    else if (!strcmp(what, "arr_on"))
        *got = proj.arr_on;
    else if (!strcmp(what, "arr_ambient"))
        *got = proj.arr_ambient;
    else if (!strcmp(what, "save_pending"))
        *got = proj_save_pending();
    else if (!strcmp(what, "seeded"))
        *got = !proj_unseeded;
    else if (!strcmp(what, "sync"))
        *got = proj.sync;
    else if (!strcmp(what, "ext_ports"))
        *got = proj.ext_ports;
    else if (!strcmp(what, "lights"))
        *got = proj.lights;
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
    else if (!strcmp(what, "tension_voice")) {   /* the first pending tension's voice, as its key; -1 none */
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
    else if (!strncmp(what, "sounding", 8)) {   /* soundingC: channel C's alone (0-15); soundingC.N: its note N */
        const char *dot = strchr(what, '.');
        for (*got = 0, c = 0; c < 16; c++)
            for (n = 0; n < 128; n++)
                *got += (int)((!what[8] || c == atoi(what + 8)) && (!dot || n == atoi(dot + 1))
                              && rt.snd[c][n >> 5] >> (n & 31) & 1u);
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
    else if (!strcmp(cmd, "dumpcount")) {
        dump_count = dump_scan((uint32_t)strtoul(a, 0, 16), (uint32_t)strtoul(b, 0, 16), (uint32_t)strtoul(c, 0, 16),
                               d[0] ? strtoll(d, 0, 10) : -1, 0, NULL, 0);
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
    else if (!strcmp(cmd, "rt")) {
        if (rt_cmd(a, b, c))
            printf("FAIL rt %s %s %s: no such control\n", a, b, c);
    } else if (!strcmp(cmd, "proj")) {
        int r = proj_cmd(a, b, c);
        if (r == -2)
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

void app_host_reboot(void)               /* what RAM holds is gone: the project, the slots, the undo stack */
{
    rt_stop();
    memset(&proj, 0, sizeof proj);
    memset(slots, 0, sizeof slots);
    undo_used = undo_n = 0;
    proj_stale = proj_unseeded = proj_saving = 0;
}
