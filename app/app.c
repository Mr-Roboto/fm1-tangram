/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan on fm1-base: the app's unity build (compiled into fm1-base's, after the platform) and the glue between
 * fm1-base's contract (app.h) and Berlinan's parts. It boots, makes the engine and draws its name; it makes no sound.
 * The engine (engine/) is compiled apart, each file its own unit (fm1.toml o2); the project (project.c), the
 * transport (runtime.c) and the pages (ui.c) are added to this unit. */
#include "live.h"
#include "project.h"

static int playing;                      /* the sequencer runs (the transport, runtime.c, will own it) */

/* ---- the engine: Berlinan's LiveEngine (76 KB) and a window's notes and CC, in the pool. The transport (runtime.c)
 * will drive it; until then app_init makes the Full ensemble's and renders its first window, the one a Play renders
 * first, so that the firmware carries the engine and runs it */
#define ENG_WINDOW 240                   /* the transport's longest window, in ticks */
#define ENG_NOTES 128                    /* a window's notes and CC (the scripts' 240 ticks hold at most 9 and 8) */
#define ENG_CC 128
static bl_project_t eng_proj PLAT_POOL;  /* the project the engine was made from */
static bl_live_t eng PLAT_POOL;
static bl_note_t eng_note[ENG_NOTES] PLAT_POOL;
static bl_cc_t eng_cc[ENG_CC] PLAT_POOL;
static int eng_nnote, eng_ncc;           /* the last window's */

static void eng_load(int tpl)            /* LiveEngine(project:) of a template (BL_TPL_) on its own seed */
{
    bl_template(tpl, &eng_proj);
    bl_live_init(&eng, &eng_proj);
}

/* the window lo..<hi (windows follow one another from 0): its notes and CC; 0, or -1 when they do not fit */
static int eng_window(bl_tick_t lo, bl_tick_t hi)
{
    eng_nnote = bl_live_render(&eng, lo, hi, eng_note, ENG_NOTES);
    eng_ncc = bl_live_render_cc(&eng, lo, hi, eng_cc, ENG_CC);
    return eng_nnote > ENG_NOTES || eng_ncc > ENG_CC ? -1 : 0;
}

#ifndef PLAT_HOST
#include "libc/libc.c"                   /* the libc the engine calls beyond felucca-bsp's (the host has its own) */
#endif

/* ---- the contract */
void app_init(void)
{
    eng_load(BL_TPL_FULL);
    eng_window(0, ENG_WINDOW);
}
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12)   /* the FM-1 makes no sound: MIDI goes out over USB */
{
    uint32_t i;
    (void)gain_q12;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}
void app_panic(void) { playing = 0; }
void app_tick(void) {}
void app_loop(void) {}

/* the screen: the name, until the pages come */
static uint8_t redraw;
void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    redraw = 1;
}
void app_input_only(void) {}
void app_frame(void)
{
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
#include "host.c"                        /* the host simulator's part: checks, the engine's self-render */
#endif
