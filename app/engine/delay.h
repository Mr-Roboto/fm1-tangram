/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
#pragma once
#include <stdint.h>

/* MIDI output effect, independent of the generator. time is 1..7 sixteenths;
 * feedback is 25/50/75/100 percent (one through four discrete echoes).
 * Zero timing/feedback/decay fields select defaults for omitted configuration. */
typedef struct {
    uint8_t enabled, time, feedback, decay;
} bl_delay_t;

enum { BL_DELAY_ENABLED, BL_DELAY_TIME, BL_DELAY_FEEDBACK, BL_DELAY_DECAY };

static inline bl_delay_t bl_delay_config(bl_delay_t d)
{
    d.enabled = d.enabled ? 1 : 0;
    if (d.time < 1 || d.time > 7) d.time = 3;
    if (d.feedback < 25 || d.feedback > 100 || d.feedback % 25) d.feedback = 75;
    if (d.decay < 50 || d.decay > 100) d.decay = 70;
    return d;
}

static inline int bl_delay_valid(bl_delay_t d)
{
    return d.enabled <= 1 && d.time >= 1 && d.time <= 7 &&
        d.feedback >= 25 && d.feedback <= 100 && !(d.feedback % 25) && d.decay >= 50 && d.decay <= 100;
}

/* Explicit live edits are strict; OFF changes only enabled, preserving feedback. */
static inline int bl_delay_edit(bl_delay_t *d, int field, int value)
{
    bl_delay_t next = *d;
    if (value < 0 || value > 255) return -1;
    switch (field) {
    case BL_DELAY_ENABLED: next.enabled = (uint8_t)value; break;
    case BL_DELAY_TIME: next.time = (uint8_t)value; break;
    case BL_DELAY_FEEDBACK: next.feedback = (uint8_t)value; break;
    case BL_DELAY_DECAY: next.decay = (uint8_t)value; break;
    default: return -1;
    }
    if (!bl_delay_valid(next)) return -1;
    *d = next;
    return 0;
}
