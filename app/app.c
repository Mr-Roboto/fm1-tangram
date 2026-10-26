/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan on fm1-base: the app's unity build (compiled into fm1-base's, after the platform) and the glue between
 * fm1-base's contract (app.h) and Berlinan's parts. The FM-1 makes no sound: the transport (runtime.c) sends the
 * engine's MIDI over USB, clocked by the 1 kHz tick (app_tick) and rendered ahead in the main loop (app_loop).
 * The engine (engine/) is compiled apart, each file its own unit (fm1.toml o2); the project (project.c), the
 * transport (runtime.c) and the pages (ui.c) are added to this unit. */
#include "live.h"
#include "project.h"

#ifndef PLAT_HOST
#include "libc/libc.c"                   /* the libc the engine calls beyond felucca-bsp's (the host has its own) */
#endif
#include "runtime.c"

/* ---- the contract */
void app_init(void) { rt_init(); }
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12)   /* the FM-1 makes no sound: MIDI goes out over USB */
{
    uint32_t i;
    (void)gain_q12;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}
void app_panic(void) { rt_stop(); }

/* the panel: PLAY plays and stops, on its press (the rest of the panel: ui.c, to come) */
static uint32_t btn_was;
static void play_button(void)
{
    uint32_t b = plat_buttons(), down = b & ~btn_was;
    btn_was = b;
    if (down >> B_PLAY & 1u) {
        if (rt_playing())
            rt_stop();
        else
            rt_play();
    }
}

/* the screen: the name, until the pages come */
static uint8_t redraw;
void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    redraw = 1;
}
void app_input_only(void) { play_button(); }
void app_frame(void)
{
    play_button();
    plat_leds(rt_playing() ? 1u << B_PLAY : 0u, 0);
    if (!redraw)
        return;
    redraw = 0;
    cv_begin(240, 40, C_BLACK);
    cv_text(10, 8, &FONT_L, APP_NAME, C_WHITE);
    cv_blit(0, 100);
}
void app_say(const char *a, const char *b)
{
    (void)a, (void)b;
}

#ifdef PLAT_HOST
#include "host.c"                        /* the host simulator's part: checks, commands, the MIDI dump */
#endif
