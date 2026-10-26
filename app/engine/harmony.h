/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Global diatonic transposition as Berlinan has it (Harmony.swift): the schedule, a timeline of degree offsets by
 * bar. An empty schedule is the pedal on the root. */
#pragma once
#include <stdint.h>
#include "bl.h"

/* TranspositionEvent */
typedef struct {
    int32_t bar, offset;        /* bar >= 0; degreeOffset */
} bl_tev_t;

/* TranspositionSchedule: sorted by bar, one event per bar */
typedef struct bl_sched {
    uint16_t n;
    bl_tev_t ev[BL_MAX_EVENTS];
} bl_sched_t;

/* TranspositionSchedule.offset(atBar:): the latest event at or before the bar, 0 before the first */
int32_t bl_sched_offset(const bl_sched_t *s, int32_t bar);
