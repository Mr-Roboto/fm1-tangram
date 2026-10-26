/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan app: the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "live.h"                        /* the engine's (engine/): a project, a state */
#include "runtime.h"

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

/* ---- the project (project.c): what SAVE keeps, as a Berlinan document (AppServices.saveDocument): the project in
 * OBJ_PROJ, each snapshot slot in its own object. The project is the app's (AppServices.project); the transport
 * runs its own copy, which a change made while playing reaches at the next Play (transportStale) */
#define PROJ_MAGIC 0x314E4C42u           /* "BLN1" */
#define PROJ_FORMAT 1u
#define PROJ_SLOTS 6                     /* UIState.snapshotSlots */
#define PROJ_UNDO 20                     /* UIState's undo stack */
enum { FORM_OTHER, FORM_CLASSIC, FORM_SLOW, FORM_AMBIENT };   /* the arranger's form presets (proj_form) */

typedef struct {
    uint32_t magic, format;
    uint8_t tpl, key_root, key_mode, sync, ext_ports, lights;   /* a BL_TPL_; 0-11; a BL_MODE_; INT 0, EXT 1; EXT's
                                                                 * input (1 << PORT_USB, 1 << PORT_TRS); lights 0
                                                                 * off, 1 keys and the buttons' glow, 2 keys only */
    uint64_t seed;                       /* masterSeed */
    int32_t bpm;                         /* 40-220 */
    uint8_t arr_has, arr_on, arr_ambient, arr_reprise;   /* Project.arrangement: nil (arr_has 0, the rest 0) or its
                                                          * config: enabled (AUTO), ambientMode, reprise */
    int16_t arr_min, arr_max;            /* its minPeriodBars ... maxPeriodBars, 1 <= min <= max */
    uint8_t density_pct, has_density;    /* density 0-100 %; has_density 0: the template's (nil: 1) */
    uint8_t channel[BL_MAX_VOICES];      /* the template's voice i's MIDI channel, 0-15 */
} proj_t;

extern proj_t proj;

void proj_defaults(void);                /* the Demo trio in A minor, its seed drawn at the first Play (proj_ready) */
uint64_t proj_fresh_seed(void);          /* UInt64.random: a template's, Dice's, the defaults' */
int proj_load(void);                     /* the project and the slots; 0 = loaded, else the defaults are in place */
int proj_save(void);                     /* the project and the slots: 0 = saved and verified; playing, 1: at the
                                          * next stop (a flash write stops everything, the MIDI clock too); -1 */
int proj_save_pending(void);             /* a SAVE waits for the stop */
void proj_to_engine(bl_project_t *out);  /* the app project as Berlinan's Project */
void proj_apply(void);                   /* stopped: the project to the transport (the Mac's rebuildTransport) */
void proj_ready(void);                   /* before a Play: the defaults' seed drawn, the project changed meanwhile
                                          * to the transport */
int proj_stopped(void);                  /* stopped: a SAVE that waited (its proj_save result, else 1), what changed
                                          * while playing to the transport (rebuildTransportIfStale) */
/* AppServices' changes: playing, what the Mac does live (0, or -1 when the engine's queue drops it) and the project
 * for the next Play; stopped, the project to the transport. -1 too for what the Mac allows only stopped */
int proj_set_template(int tpl);          /* applyTemplate: a fresh seed; playing, it takes over at the next bar */
int proj_set_key(int root, int mode);    /* applyLiveMusicalChanges: playing, at the next bar */
void proj_set_bpm(int32_t bpm);          /* the same: playing, at the next bar */
int proj_set_density(int pct);           /* setDensity: playing, at the next bar */
int proj_set_arranger(int on);           /* setArrangerEnabled (AUTO): playing, at the next bar */
int proj_set_form(int form);             /* setArrangerForm: a FORM_ preset, stopped only */
int proj_set_ambient(int on);            /* setAmbientMode: stopped only */
int proj_form(void);                     /* the arrangement's preset (enabled aside), FORM_OTHER for none */
void proj_set_channel(int voice, int ch);   /* setVoiceChannel: the template's voice i, 0-15 */
int proj_set_sync(int ext);              /* stopped only */
void proj_set_ext_ports(int mask);
int proj_dice(void);                     /* UIState.newSeed: an undo point, then AppServices.reseed */
int slot_store(int k);                   /* saveSnapshot: the state at the render head; 0, -1 */
int slot_recall(int k);                  /* recallSnapshot: an undo point, the state at the next bar; 0, -1 */
int slot_used(int k);
int undo_push(void);                     /* the undo point before every destructive action (regenerate, reroll,
                                          * recall, Dice), 20 deep, the oldest dropped; -1: none taken (a state the
                                          * format cannot hold) */
int undo_refused(void);                  /* the last undo point was not taken (the pushes inside recall and Dice) */
int undo_pop(void);                      /* the last undo point recalled at the next bar; -1: none (or dropped) */
int undo_depth(void);                    /* the undo points held (UIState.canUndo: more than none) */

/* the panel and the screen (ui.c) */
void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
void ui_say(const char *a, const char *b);

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

/* the host simulator's part (app/host.c): Berlinan's checks, its commands, a reboot */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);   /* 1 = handled */
#define APP_HOST_REBOOT
void app_host_reboot(void);              /* the project forgotten, as RAM is at a restart */
