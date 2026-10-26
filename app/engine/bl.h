/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan's engine in C: the types and capacities every part shares. Ticks are Swift's Tick (Int): 960 to the
 * quarter note. */
#pragma once
#include <stdint.h>

typedef int64_t bl_tick_t;

#define BL_PPQN 960
#define BL_TICKS_PER_BAR 3840   /* 4/4, the only meter the templates use */

/* capacities of what Swift keeps in arrays */
#define BL_MAX_VOICES 7
#define BL_MAX_NOTES 64         /* notes of one pattern */
#define BL_MAX_EVENTS 256       /* transposition events */
#define BL_MAX_LANES 2          /* CC lanes of one voice */
#define BL_MAX_SCENES 5

typedef struct {
    int32_t beats, unit;        /* beatsPerBar, beatUnit */
} bl_meter_t;

static inline int32_t bl_ticks_per_beat(bl_meter_t m) { return 4 * BL_PPQN / m.unit; }
static inline int32_t bl_ticks_per_bar(bl_meter_t m) { return m.beats * bl_ticks_per_beat(m); }
