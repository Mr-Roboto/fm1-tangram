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
} cmp_t;

static int compare(void *ctx, const char *text)
{
    cmp_t *c = ctx;
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
    int r = bl_script_play(&s, 0, compare, &c, err, sizeof err);
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
