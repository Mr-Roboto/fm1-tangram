/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The transposition schedule (harmony.h). */
#include "harmony.h"

/* TranspositionSchedule.offset(atBar:) */
int32_t bl_sched_offset(const bl_sched_t *s, int32_t bar)
{
    int32_t result = 0;
    for (int i = 0; i < s->n; i++) {
        if (s->ev[i].bar > bar) break;
        result = s->ev[i].offset;
    }
    return result;
}
