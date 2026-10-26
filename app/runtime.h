/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The transport's controls (runtime.c), the main loop's: what the project (project.c) and the pages call, and what
 * the project's host test (tests/host/project_test.c) stands in for. */
#pragma once
#include <stdint.h>
#include "live.h"

void rt_init(void);                      /* the Full ensemble until the project gives one */
void rt_play(void);                      /* Transport.play: always from tick 0, a fresh engine */
void rt_stop(void);                      /* Transport.stop */
int rt_playing(void);
bl_tick_t rt_tick(void);                 /* the tick sounding now (0 before tick 0) */
bl_tick_t rt_head(void);                 /* the render head */
const bl_live_t *rt_engine(void);
void rt_capture(bl_state_t *out);        /* Transport.captureState */
int rt_muted(int vid);
int32_t rt_bpm(void);
int rt_action(const bl_action_t *a);     /* Transport.enqueue: 0, or -1 when the engine's queue drops it */
void rt_set_tempo(int32_t bpm);          /* playing: from the head's next bar line */
void rt_load(const bl_project_t *p);     /* playing: Transport.loadProject; stopped: the next Play's project */
void rt_set_mute(int vid, int on);
void rt_set_channel(int vid, uint8_t ch);
void rt_set_sync(int ext);               /* INT 0, EXT 1: stopped only */
void rt_set_ext_port(int mask);          /* bits 1 << PORT_USB, 1 << PORT_TRS */
