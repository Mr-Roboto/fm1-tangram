/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan app: the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "BERLINAN"
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz (the FM-1 makes no sound) */
#define APP_TICK                         /* the 1 kHz tick: the sequencer's clock (app_tick) */
#define APP_LOOP                         /* every pass of the main loop: the sequencer's queue (app_loop) */
#ifdef APP_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define BL_VERSION APP_VERSION
#else
#define BL_VERSION "DEV"
#endif

/* the store: the project, the panel calibration (the platform's, at 0xDC000 as AMB-1) and the six slots, two
 * 4 KiB sectors each, in the data area 0x97000..0xDFFFF */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_SLOT0, OBJ_SLOT1, OBJ_SLOT2, OBJ_SLOT3, OBJ_SLOT4, OBJ_SLOT5, OBJ_NOBJ };
#define APP_STORE_MAP {0xDE000u, 0xDC000u, 0xDA000u, 0xD8000u, 0xD6000u, 0xD4000u, 0xD2000u, 0xD0000u}

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the saved project (or the defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12);   /* audio interrupt: n stereo frames; silence */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
void app_tick(void);
void app_loop(void);

/* the host simulator's part (app/host.c): Berlinan's checks for "expect" */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
