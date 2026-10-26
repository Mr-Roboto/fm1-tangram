/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): Berlinan's checks for
 * "expect", its commands and a reboot.
 *
 * Berlinan's checks: playing selfnotes selfhash_hi selfhash_lo (fm1-base's own: store_writes clocks_out midi_out
 * ...). Its command: selfrender TEMPLATE BARS (demo full ambient: the template on its own seed, rendered by the app's
 * engine for BARS bars in windows of 240 ticks, from tick 0 to the end or the arranger's ending; selfnotes its notes,
 * selfhash_hi/_lo the two halves of their notes-hash: FNV-1a 64 over the N lines of tests/equiv/README.md, each with
 * its "\n", the value tests/host/equiv_test.c prints for a script of the same). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int self_notes = -1;              /* the last selfrender's notes, -1 none (or it failed) */
static uint64_t self_hash;

static uint64_t fnv(uint64_t h, const char *s)
{
    for (; *s; s++)
        h = (h ^ (uint8_t)*s) * UINT64_C(0x100000001b3);
    return h;
}

static void selfrender(const char *tpl, const char *bars)
{
    char line[160];
    int t = 0, i;
    bl_tick_t total = (bl_tick_t)atoi(bars) * BL_TICKS_PER_BAR, head, end, stop;
    while (t < BL_NTPL && strcmp(BL_TPL_NAME[t], tpl))
        t++;
    self_notes = -1;
    if (t == BL_NTPL || total < 1) {
        printf("FAIL selfrender %s %s: no such template, or no bars\n", tpl, bars);
        return;
    }
    eng_load(t);
    self_notes = 0;
    self_hash = UINT64_C(0xcbf29ce484222325);
    for (head = 0; head < total; head = end) {
        end = (head / ENG_WINDOW + 1) * ENG_WINDOW;
        if (end > total)
            end = total;
        if (eng_window(head, end)) {
            printf("FAIL selfrender: tick %lld: %d notes and %d CC, more than a window holds\n", (long long)head,
                   eng_nnote, eng_ncc);
            self_notes = -1;
            return;
        }
        for (i = 0; i < eng_nnote; i++) {
            const bl_note_t *q = &eng_note[i];
            snprintf(line, sizeof line, "N %lld %s %d %d %lld %d %d\n", (long long)q->at,
                     eng.project.voice[q->voice].id, q->midi, q->velocity, (long long)q->gate, q->accent, q->ratchet);
            self_hash = fnv(self_hash, line);
            self_notes++;
        }
        stop = bl_live_stop_tick(&eng);  /* the arranger's ending: the transport stops there */
        if (stop >= 0 && end >= stop)
            break;
    }
    printf("  selfrender %s %s bars: %d notes, notes-hash %016llx\n", tpl, bars, self_notes,
           (unsigned long long)self_hash);
}

int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "playing"))
        *got = playing;                  /* 0 until the transport (runtime.c) */
    else if (!strcmp(what, "selfnotes"))
        *got = self_notes;
    else if (!strcmp(what, "selfhash_hi"))   /* (expect reads a hex value into an int: the same 32 bits) */
        *got = (int)(uint32_t)(self_hash >> 32);
    else if (!strcmp(what, "selfhash_lo"))
        *got = (int)(uint32_t)self_hash;
    else
        return 0;
    return 1;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)c, (void)d;
    if (!strcmp(cmd, "selfrender")) {
        selfrender(a, b);
        return 1;
    }
    return 0;
}

void app_host_reboot(void) {}            /* nothing is kept in RAM yet (app_init makes the engine anew) */
