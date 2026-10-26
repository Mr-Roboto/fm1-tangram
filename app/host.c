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
 *                            C, 0-15); events the full queue refused (and notes or CC past a window's buffer)
 *   midicmp midilines        the last cmpmidi: 0 the same, else the first line that differs (-1: a file missing);
 *                            the lines that matched
 *   selfnotes selfhash_hi selfhash_lo   the last selfrender
 * Its commands:
 *   selfrender TEMPLATE BARS (stopped) the template on its own seed, rendered by the app's engine for BARS bars in
 *                            windows of 240 ticks, from tick 0 to the end or the arranger's ending; selfnotes its
 *                            notes, selfhash_hi/_lo the two halves of their notes-hash: FNV-1a 64 over the N lines of
 *                            tests/equiv/README.md, each with its "\n", the value tests/host/equiv_test.c prints
 *   selfscript FILE          (stopped) a .midiscript for the next Play: its project, its actions as rt_action at
 *                            their ticks (a window ends at each), its mutes as muteat, the render held at its end.
 *                            Only actions that are one engine action each (no reroll, save, recall, tension, scene,
 *                            reseed, load)
 *   muteat VOICE TICK 0|1    a test's mute: for the windows that start at or after TICK, nothing flushed (the rule
 *                            of RefTool's midi mode)
 *   dumpmidi [FILE]          start (FILE) or stop logging "tick status d1 d2" (hex bytes) for every message the
 *                            transport sends, in send order; a flush's messages carry the sounding tick
 *   cmpmidi FILE             the dump so far against FILE (a .midiref), line by line: midicmp, midilines
 *   rt play | stop | tempo BPM | load TEMPLATE [SEED] | mute VOICE 0|1 | channel VOICE CH | sync int|ext |
 *      port USB|TRS|both     the transport's controls (VOICE a voice id, CH 0-15), until the panel has them all */

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

static int ss_supported(const bl_sact_t *a)   /* the actions that are one engine action each */
{
    switch (a->kind) {
    case BL_SA_TRANSPOSE: case BL_SA_REGENERATE: case BL_SA_BLANK: case BL_SA_LOCK: case BL_SA_KEY:
    case BL_SA_DENSITY: case BL_SA_BPART: case BL_SA_VOICE: case BL_SA_ARRANGER: case BL_SA_ADVANCE:
        return 1;
    }
    return 0;
}

static void ss_apply(const bl_sact_t *s)  /* as script.c's scr_apply makes them */
{
    static const uint8_t KIND[] = {
        [BL_SA_TRANSPOSE] = BL_A_TRANSPOSE, [BL_SA_REGENERATE] = BL_A_REGENERATE, [BL_SA_BLANK] = BL_A_BLANK,
        [BL_SA_LOCK] = BL_A_LOCK, [BL_SA_KEY] = BL_A_KEY, [BL_SA_DENSITY] = BL_A_DENSITY, [BL_SA_BPART] = BL_A_BPART,
        [BL_SA_VOICE] = BL_A_VOICE_ACTIVE, [BL_SA_ARRANGER] = BL_A_ARRANGER, [BL_SA_ADVANCE] = BL_A_ADVANCE,
    };
    bl_action_t a;
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
static int midi_cmp = -1, midi_lines;

static void host_sent(bl_tick_t tick, uint32_t pkt)
{
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
    } else if (!strcmp(a, "mute") && bl_vid(b) != BL_VID_NONE)
        rt_set_mute(bl_vid(b), atoi(c));
    else if (!strcmp(a, "channel") && bl_vid(b) != BL_VID_NONE)
        rt_set_channel(bl_vid(b), (uint8_t)atoi(c));
    else if (!strcmp(a, "sync") && (!strcmp(b, "int") || !strcmp(b, "ext")))
        rt_set_sync(b[0] == 'e');
    else if (!strcmp(a, "port") && (!strcmp(b, "USB") || !strcmp(b, "TRS") || !strcmp(b, "both")))
        rt_set_ext_port(b[0] == 'U' ? 1 << PORT_USB : b[0] == 'T' ? 1 << PORT_TRS : 1 << PORT_USB | 1 << PORT_TRS);
    else
        return -1;
    return 0;
}

int app_expect(const char *what, int *got)
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
    else if (!strncmp(what, "sounding", 8))   /* soundingC: channel C's alone (0-15) */
        for (*got = 0, c = 0; c < 16; c++)
            for (n = 0; n < 128; n++)
                *got += (int)((!what[8] || c == atoi(what + 8)) && rt.snd[c][n >> 5] >> (n & 31) & 1u);
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
    (void)d;
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
    else if (!strcmp(cmd, "rt")) {
        if (rt_cmd(a, b, c))
            printf("FAIL rt %s %s %s: no such control\n", a, b, c);
    } else
        return 0;
    return 1;
}

void app_host_reboot(void) { rt_stop(); }   /* nothing is kept in RAM (app_init makes the transport anew) */
