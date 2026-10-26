/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan on fm1-base: the app's unity build (compiled into fm1-base's, after the platform) and the glue between
 * fm1-base's contract (app.h) and Berlinan's parts. The FM-1 makes no sound: the transport (runtime.c) sends the
 * engine's MIDI over USB, clocked by the 1 kHz tick (app_tick) and rendered ahead in the main loop (app_loop).
 * The engine (engine/) is compiled apart, each file its own unit (fm1.toml o2); the transport (runtime.c), the
 * project (project.c) and the panel and the pages (ui.c) are added to this unit. */
#include "live.h"
#include "project.h"

#ifndef PLAT_HOST
#include "libc/libc.c"                   /* the libc the engine calls beyond felucca-bsp's (the host has its own) */
#endif
#include "runtime.c"
#include "project.c"
#include "ui.c"

/* ---- the contract */
void app_init(void)                      /* the transport, then the saved project (or the defaults) on it */
{
    rt_init();
    proj_load();
    proj_apply();
}
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12)   /* the FM-1 makes no sound: MIDI goes out over USB */
{
    uint32_t i;
    (void)gain_q12;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}
void app_panic(void) { rt_stop(); }
void app_ui_init(void) { ui_init(); }
void app_frame(void) { ui_frame(); }
void app_input_only(void) { ui_input_only(); }
void app_say(const char *a, const char *b) { ui_say(a, b); }

#ifdef PLAT_HOST
#include "host.c"                        /* the host simulator's part: checks, commands, the MIDI dump */
#endif
