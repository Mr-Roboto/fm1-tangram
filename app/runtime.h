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
int rt_idle(void);                       /* stopped, and what the stop sends gone out: a flash write may come */
bl_tick_t rt_tick(void);                 /* the tick sounding now (0 before tick 0) */
bl_tick_t rt_head(void);                 /* the render head */
bl_tick_t rt_play_tick(void);            /* the tick sounding now, in the running engine's ticks (the panel's playheads) */
/* Latest dispatched original source/event tick + 1; zero during silence.
 * Release/retrigger clears it; echoes never select a source. */
uint32_t rt_source_onset(int vid);
/* Dispatch-only event latch for visual persistence; echoes and offs do not
 * advance the serial. The last position persists until a new original or Stop;
 * position is zero when stopped or the latest original has no source metadata. */
uint32_t rt_source_event(int vid, uint32_t *onset);
/* Same original dispatch latch plus onset-grid identity; both read atomically. */
uint32_t rt_source_event_grid(int vid, uint32_t *onset, uint32_t *grid);
int rt_loading(void);                    /* playing: a loaded project waits for the head's next bar line */
int rt_channel(int vid);                 /* the transport's channel of voice vid (a BL_VID_), 0-15; -1 none */
const bl_live_t *rt_engine(void);
void rt_capture(bl_state_t *out);        /* Transport.captureState */
int rt_muted(int vid);
int32_t rt_bpm(void);
int rt_action(const bl_action_t *a);     /* Transport.enqueue: 0, or -1 when the engine's queue drops it */
/* Main-loop musical configuration, BL_VID_ addressing. No MIDI queue edits. */
int rt_get_voice_config(int vid, bl_vconfig_t *active, bl_vconfig_t *target);
int rt_has_voice_state(void);            /* materialized engine, rather than a freshly loaded project preview */
int rt_set_voice_config(int vid, const bl_vconfig_t *target);
int rt_set_voice_config_field(int vid, int field, int32_t value);
int rt_set_composer(const bl_compcfg_t *cfg);
int rt_select_composer_stage(int section);
void rt_set_tempo(int32_t bpm);          /* playing: from the head's next bar line */
void rt_load(const bl_project_t *p);     /* playing: Transport.loadProject; stopped: the next Play's project */
void rt_set_mute(int vid, int on);
/* Next actually dispatched original, including originals already in lookahead.
 * Echoes and concrete offs are immutable. Delay setters reject invalid values. */
int rt_get_delay(int vid, bl_delay_t *out);
int rt_set_delay(int vid, const bl_delay_t *settings);
int rt_set_delay_field(int vid, int field, int value);
void rt_set_channel(int vid, uint8_t ch);
void rt_set_sync(int ext);               /* INT 0, EXT 1: stopped only */
void rt_set_ext_port(int mask);          /* bits 1 << PORT_USB, 1 << PORT_TRS */
/* Device-settings backend only; projects never call this. Mode edits are
 * stopped-only; unchanged mode/port application does not reset the clock. */
int rt_apply_clock_settings(int ext, int ports);
