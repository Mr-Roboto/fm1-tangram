/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan on fm1-base: the app's unity build (compiled into fm1-base's, after the platform) and the glue between
 * fm1-base's contract (app.h) and Berlinan's parts. The skeleton: it boots, draws its name and makes no sound; the
 * engine (engine/), the project (project.c), the transport (runtime.c) and the pages (ui.c) are added to it. */

static int playing;                      /* the sequencer runs (the transport, runtime.c, will own it) */

/* ---- the contract */
void app_init(void) {}
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
#include "host.c"                        /* the host simulator's part: checks */
#endif
