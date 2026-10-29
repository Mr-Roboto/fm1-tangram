/* SPDX-License-Identifier: GPL-2.0-or-later */
/* TGM-001A: real project validation/storage and UI input on host stubs.
 * Screen drawing, speech output and transport execution are outside this test. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "test.h"

static uint8_t saved_project[sizeof(proj_t)];
static int saved_len, playing, algo_delta, actions;
static bl_project_t loaded;
static bl_action_t last_action;
uint32_t plat_ms(void) { return 1000; }
uint32_t plat_master(void) { return 2048; }
uint32_t plat_buttons(void) { return 0; }
uint32_t plat_keys(void) { return 0; }
int32_t plat_enc(int role)
{
    if (role != EN_ALGO) return 0;
    int d = algo_delta;
    algo_delta = 0;
    return d;
}
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    if (obj != OBJ_PROJ || !saved_len) return -1;
    uint32_t n = (uint32_t)saved_len < max ? (uint32_t)saved_len : max;
    memcpy(dst, saved_project, n);
    return (int)n;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t n)
{
    if (obj != OBJ_PROJ || n > sizeof saved_project) return -1;
    memcpy(saved_project, src, n);
    saved_len = (int)n;
    return 0;
}
int rt_playing(void) { return playing; }
int rt_idle(void) { return !playing; }
void rt_load(const bl_project_t *p) { loaded = *p; }
int rt_action(const bl_action_t *a) { last_action = *a; actions++; return 0; }
void rt_capture(bl_state_t *out) { bl_initial_state(&loaded, out); }
void rt_set_tempo(int32_t bpm) { (void)bpm; }
void rt_set_channel(int vid, uint8_t ch) { (void)vid; (void)ch; }
void rt_set_sync(int ext) { (void)ext; }
void rt_set_ext_port(int mask) { (void)mask; }
void rt_set_mute(int vid, int on) { (void)vid; (void)on; }
int rt_muted(int vid) { (void)vid; return 0; }
void rt_play(void) { playing = 1; }
void rt_stop(void) { playing = 0; }
const bl_live_t *rt_engine(void) { return NULL; }
static int plat_host_exact = 1;
#define ANN_NOW 1u
#define ANN_KEEP 2u
static int ann_on(void) { return 0; }
static void ann_say(const char *a, const char *b, uint32_t flags) { (void)a; (void)b; (void)flags; }
static void ann_tick(uint32_t now) { (void)now; }
void plat_lights(uint32_t b, uint32_t k, uint32_t db, uint32_t dk, int glow)
{ (void)b; (void)k; (void)db; (void)dk; (void)glow; }

#include "../../app/project.c"
static void host_saving(void) {}
#include "../../app/ui.c"
static void screen_init(void) {}
static void lanes(void) {}
static void draw_header(void) {}
static void draw_main(void) {}
static void draw_knobs(void) {}
static void speak(void) {}

static void test_settings(void)
{
    proj_defaults();
    CHECK(proj.key_root == 9 && proj.key_mode == BL_MODE_NATURAL_MINOR, "default A natural minor");
    for (int live = 0; live < 2; live++) {
        playing = live;
        for (int root = 0; root < 12; root++) {
            for (int mode = 0; mode < 23; mode++) {
                CHECK(proj_set_key(root, mode) == 0, "select %d/%d live=%d", root, mode, live);
                CHECK(proj.key_root == root && proj.key_mode == mode && proj_valid(&proj), "settings accepted");
                bl_project_t p;
                proj_to_engine(&p);
                CHECK(p.key.root == root && p.key.scale.mode == mode && p.key.scale.count >= 5,
                      "engine key %d/%d", root, mode);
                if (live) {
                    CHECK(last_action.kind == BL_A_KEY && last_action.key.root == root &&
                          last_action.key.scale.mode == mode, "live key action");
                } else {
                    CHECK(loaded.key.root == root && loaded.key.scale.mode == mode, "stopped key");
                    CHECK(proj_save() == 0, "save current settings");
                    proj_defaults();
                    CHECK(proj_load() == 0 && proj.key_root == root && proj.key_mode == mode,
                          "load current settings %d/%d", root, mode);
                }
            }
        }
        const int bad_mode[] = {-1, 23, 24, 255};
        for (unsigned i = 0; i < sizeof bad_mode / sizeof bad_mode[0]; i++) {
            proj_t before = proj;
            int n = actions;
            CHECK(proj_set_key(9, bad_mode[i]) == -1 && !memcmp(&proj, &before, sizeof proj) && actions == n,
                  "reject invalid selection %d", bad_mode[i]);
        }
        CHECK(proj_set_key(-1, 0) == -1 && proj_set_key(12, 0) == -1, "reject invalid roots");
    }
    playing = 0;
    proj.key_mode = 23;
    CHECK(!proj_valid(&proj), "mode 23 rejected");
    memcpy(saved_project, &proj, sizeof proj);
    saved_len = sizeof proj;
    CHECK(proj_load() != 0 && proj.key_mode == BL_MODE_NATURAL_MINOR && proj.key_root == 9,
          "invalid current setting resets to defaults");
}

static void test_algorithm(void)
{
    static const char *const NEW_NAMES[] = {"PMAJ", "EGYP", "HIRA", "MELM", "HMAJ", "ALTR", "GYPS",
                                          "ROMN", "BHAI", "MARV", "PURV", "TODI", "ARAB"};
    for (int m = 0; m < 23; m++) {
        CHECK(MODE_NAME[m] && MODE_NAME[m][0] && strlen(MODE_NAME[m]) <= 23, "UI label %d", m);
        if (m >= 10) CHECK(!strcmp(MODE_NAME[m], NEW_NAMES[m - 10]), "new UI label %d", m);
    }
    for (int live = 0; live < 2; live++) {
        playing = live;
        for (int root = 0; root < 12; root++) {
            proj_defaults();
            CHECK(proj_set_key(root, 0) == 0, "initial key");
            ui_init();
            const int deltas[] = {1, -1, 7, -7};
            for (unsigned j = 0; j < sizeof deltas / sizeof deltas[0]; j++) {
                int mode = 0;
                for (int i = 0; i < 23; i++) {
                    algo_delta = deltas[j];
                    ui_input_only();
                    mode = (mode + (deltas[j] > 0 ? 1 : -1) + 23) % 23;
                    CHECK(proj.key_mode == mode && proj.key_root == root, "ALGORITHM %d/%d", root, mode);
                    CHECK(!strcmp(ui.msg[0], "MODE") && !strcmp(ui.msg[1], MODE_NAME[mode]), "mode announcement");
                }
                CHECK(proj.key_mode == 0, "full cycle wraps to zero");
            }
            ui_input_only();
            CHECK(proj.key_mode == 0, "zero delta keeps mode");
        }
    }
    playing = 0;
}

int main(void)
{
    test_settings();
    test_algorithm();
    printf(fails ? "scale app: %d FAILED\n" : "scale app: ok (settings, storage, ALGORITHM)\n", fails);
    return fails != 0;
}
