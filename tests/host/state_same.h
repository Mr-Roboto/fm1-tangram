/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The host tests' comparison of two engine states (engine_test, project_test). */
#pragma once
#include <string.h>
#include "live.h"

/* two states the same, field by field (the ids by their text, a pattern's notes up to its n) */
static int state_same(const bl_state_t *a, const bl_state_t *b)
{
    if (a->n != b->n || a->sched.n != b->sched.n) return 0;
    for (int i = 0; i < a->sched.n; i++)
        if (a->sched.ev[i].bar != b->sched.ev[i].bar || a->sched.ev[i].offset != b->sched.ev[i].offset) return 0;
    for (int i = 0; i < a->n; i++) {
        const bl_voice_t *x = &a->v[i], *y = &b->v[i];
        const bl_pattern_t *p = &x->pat, *q = &y->pat;
        if (strcmp(x->id, y->id) || x->role != y->role || x->reg_low != y->reg_low || x->reg_high != y->reg_high ||
            x->participates != y->participates || x->anchor != y->anchor || x->ref_c != y->ref_c ||
            a->generation[i] != b->generation[i])
            return 0;
        if (p->length != q->length || p->start_offset != q->start_offset || p->n != q->n ||
            p->generator != q->generator || p->locked != q->locked || p->remaining != q->remaining ||
            p->seed != q->seed)
            return 0;
        for (int k = 0; k < p->n; k++) {
            const bl_pnote_t *m = &p->note[k], *o = &q->note[k];
            if (m->position != o->position || m->gate != o->gate || m->pitch.degree != o->pitch.degree ||
                m->pitch.octave != o->pitch.octave || m->pitch.accidental != o->pitch.accidental ||
                m->velocity != o->velocity || m->accent != o->accent || m->ratchet != o->ratchet)
                return 0;
        }
    }
    return 1;
}
