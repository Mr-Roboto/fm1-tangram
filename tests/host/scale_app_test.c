/* SPDX-License-Identifier: GPL-2.0-or-later */
/* TGM-001A: real project validation/storage and UI input on host stubs.
 * Screen drawing, speech output and transport execution are outside this test. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "test.h"

static uint8_t scale_fixture_store[OBJ_NOBJ][PLAT_STORE_MAX];
static int saved_len[OBJ_NOBJ], playing, algo_delta, preset_delta, actions, stage_calls;
static bl_project_t loaded, next_project;
static bl_live_t engine;
static int fresh = 1, has_next;
static int clock_sync, clock_ports = DEVICE_CLOCK_BOTH, clock_bpm = 96;
static bl_action_t last_action;
uint32_t plat_ms(void) { return 1000; }
uint32_t plat_master(void) { return 2048; }
uint32_t plat_buttons(void) { return 0; }
uint32_t plat_keys(void) { return 0; }
int32_t plat_enc(int role)
{
    if (role == EN_PRESET) { int d = preset_delta; preset_delta = 0; return d; }
    if (role != EN_ALGO) return 0;
    int d = algo_delta;
    algo_delta = 0;
    return d;
}
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    if (obj >= OBJ_NOBJ || !saved_len[obj]) return -1;
    uint32_t n = (uint32_t)saved_len[obj] < max ? (uint32_t)saved_len[obj] : max;
    memcpy(dst, scale_fixture_store[obj], n);
    return (int)n;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t n)
{
    if (obj >= OBJ_NOBJ || n > PLAT_STORE_MAX) return -1;
    memcpy(scale_fixture_store[obj], src, n);
    saved_len[obj] = (int)n;
    return 0;
}
int rt_playing(void) { return playing; }
int rt_idle(void) { return !playing; }
/* Like project_test's transport double: materialize on demand, stage live
 * frames through the actual engine, and distinguish them from future targets.
 * No playback clock or MIDI scheduler is simulated by this fixture. */
static void materialize_loaded(void)
{
    if (fresh) { bl_live_init(&engine, &loaded); fresh = 0; }
}
static int loaded_voice(int vid)
{
    if (vid < 0 || vid >= BL_NVIDS) return -1;
    for (int i = 0; i < loaded.nvoices; i++) if (bl_vid(loaded.voice[i].id) == vid) return i;
    return -1;
}
void rt_load(const bl_project_t *p)
{
    if (playing) { next_project = *p; has_next = 1; return; }
    loaded = *p; fresh = 1; has_next = 0;
    if (!clock_sync) clock_bpm = p->bpm;
}
int rt_loading(void) { return playing && has_next; }
int rt_has_voice_state(void) { return !fresh; }
int rt_get_voice_config(int vid, bl_vconfig_t *active, bl_vconfig_t *target)
{
    if (loaded_voice(vid) < 0) return -1;
    materialize_loaded();
    return bl_live_get_config(&engine, vid, active, target);
}
int rt_set_voice_config(int vid, const bl_vconfig_t *target)
{
    int i = loaded_voice(vid);
    if (i < 0 || !target || !bl_config_valid(target) || rt_loading()) return -1;
    if (playing) {
        materialize_loaded();
        if (bl_live_set_config(&engine, vid, target)) return -1;
    }
    loaded.voice[i].has_config = 1;
    loaded.voice[i].config = *target;
    if (!playing) fresh = 1;
    return 0;
}
int rt_get_delay(int vid, bl_delay_t *out)
{
    int i = loaded_voice(vid);
    if (i < 0 || !out) return -1;
    materialize_loaded();
    *out = engine.st[i].voice.delay;
    return 0;
}
int rt_set_delay(int vid, const bl_delay_t *settings)
{
    int i = loaded_voice(vid);
    if (i < 0 || !settings || !bl_delay_valid(*settings) || rt_loading()) return -1;
    if (playing) materialize_loaded();
    loaded.voice[i].delay = *settings;
    if (playing) {
        engine.st[i].voice.delay = *settings;
        engine.project.voice[i].delay = *settings;
    } else fresh = 1;
    return 0;
}
int rt_action(const bl_action_t *a)
{
    if (!playing) return 0;
    materialize_loaded();
    if (bl_live_enqueue(&engine, a, engine.rendered)) return -1;
    last_action = *a; actions++;
    return 0;
}
void rt_capture(bl_state_t *out)
{
    if (fresh) bl_initial_state(&loaded, out);
    else bl_live_capture(&engine, out);
}
void rt_set_tempo(int32_t bpm) { if (!clock_sync) clock_bpm = bpm; }
void rt_set_channel(int vid, uint8_t ch)
{
    int i = loaded_voice(vid);
    if (i < 0 || ch > 15) return;
    loaded.voice[i].channel = ch;
    if (!fresh) {
        engine.st[i].voice.midi_channel = ch;
        engine.project.voice[i].channel = ch;
    }
}
int rt_apply_clock_settings(int ext, int ports)
{
    if ((ext != 0 && ext != 1) || !ports || (ports & ~DEVICE_CLOCK_BOTH) ||
        (ext != clock_sync && playing)) return -1;
    if (ext != clock_sync && !ext) clock_bpm = loaded.bpm;
    clock_sync = ext; clock_ports = ports;
    return 0;
}
void rt_set_sync(int ext)
{
    device_settings_t settings = device_settings_get();
    settings.sync = ext ? DEVICE_SYNC_EXTERNAL : DEVICE_SYNC_INTERNAL;
    device_settings_set(settings);
}
void rt_set_ext_port(int mask)
{
    device_settings_t settings = device_settings_get();
    settings.ext_ports = (uint8_t)mask;
    device_settings_set(settings);
}
void plat_log(const char *line) { fprintf(stderr, "%s\n", line); }
void rt_set_mute(int vid, int on) { (void)vid; (void)on; }
int rt_muted(int vid) { (void)vid; return 0; }
void rt_play(void) { materialize_loaded(); playing = 1; }
void rt_stop(void) { playing = 0; }
const bl_live_t *rt_engine(void) { materialize_loaded(); return &engine; }
int rt_select_composer_stage(int section)
{
    stage_calls++;
    if (playing) { materialize_loaded(); return bl_live_select_composer_stage(&engine, section, engine.rendered); }
    loaded.composer_start = (uint8_t)section;
    return 0;
}
int rt_set_composer(const bl_compcfg_t *cfg)
{
    if (playing) {
        materialize_loaded();
        if (bl_live_set_composer(&engine, cfg, engine.rendered)) return -1;
    }
    loaded.composer = *cfg;
    if (!playing) fresh = 1;
    return 0;
}
static int plat_host_exact = 1;
#define ANN_NOW 1u
#define ANN_KEEP 2u
static int ann_on(void) { return 0; }
static void ann_say(const char *a, const char *b, uint32_t flags) { (void)a; (void)b; (void)flags; }
static void ann_tick(uint32_t now) { (void)now; }
void plat_lights(uint32_t b, uint32_t k, uint32_t db, uint32_t dk, int glow)
{ (void)b; (void)k; (void)db; (void)dk; (void)glow; }

#include "../../app/device_settings.c"
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
    memcpy(scale_fixture_store[OBJ_PROJ], &proj, sizeof proj);
    saved_len[OBJ_PROJ] = sizeof proj;
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
            ui.view = V_SETUP; /* ALGORITHM's existing global-mode mapping is outside HOME. */
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

static void test_drum_controls(void)
{
    for (int live = 0; live < 2; live++) {
        playing = 0;
        proj_defaults();
        proj_set_template(BL_TPL_FULL);
        ui_init();
        playing = live;
        ui.sel = NVKEYS - 1; /* existing RHYTHM/Heartbeat voice */
        CHECK(heartbeat_selected(), "Heartbeat UI follows selected generator");
        ui.page[V_PERFORM] = 0;
        ui.page[V_VOICE] = 1;
        CHECK(view_knob(V_PERFORM, 0) == KN_TRACK && view_knob(V_PERFORM, 1) == KN_GENERATOR,
              "drum HOME preserves track/generator controls");
        CHECK(view_knob(V_PERFORM, 2) == KN_DRUM_PARTS && view_knob(V_PERFORM, 3) == KN_DRUM_GROOVE,
              "drum HOME assigns PARTS/GROOVE instead of Root/Scale");
        CHECK(view_knob(V_VOICE, 1) == KN_NONE && view_knob(V_VOICE, 0) == KN_CHANNEL &&
              view_knob(V_VOICE, 2) == KN_VDENSITY && view_knob(V_VOICE, 3) == KN_COMP_TENSION,
              "drum ROUTING hides Register and preserves valid controls");
        bl_vconfig_t before = {0}, after = {0};
        int voice = key_voice(ui.sel), root = proj.key_root, mode = proj.key_mode;
        CHECK(!proj_get_voice_config(voice, &before), "capture drum controls");
        CHECK(before.gen.direction == BL_DRUM_ALL && before.gen.groove == BL_DRUM_STEADY,
              "drum controls default to ALL / STEADY");
        knob(EN_K3, view_knob(V_PERFORM, 2), 1);
        CHECK(!proj_get_voice_config(voice, &after) && after.gen.direction == BL_DRUM_ALL,
              "PARTS clamps at ALL");
        for (int parts = 3; parts >= 1; parts--) {
            knob(EN_K3, view_knob(V_PERFORM, 2), -1);
            CHECK(!proj_get_voice_config(voice, &after) && bl_drum_parts(after.gen.direction) == parts,
                  "PARTS selects instrument count %d", parts);
        }
        knob(EN_K3, view_knob(V_PERFORM, 2), -1);
        CHECK(!proj_get_voice_config(voice, &after) && after.gen.direction == BL_DRUM_BD, "PARTS clamps at BD");
        for (int parts = 2; parts <= 4; parts++) knob(EN_K3, view_knob(V_PERFORM, 2), 1);
        knob(EN_K4, view_knob(V_PERFORM, 3), -1);
        knob(EN_K4, view_knob(V_PERFORM, 3), -1);
        CHECK(!proj_get_voice_config(voice, &after) && after.gen.groove == BL_DRUM_SPARSE, "GROOVE clamps at SPARSE");
        knob(EN_K4, view_knob(V_PERFORM, 3), 1);
        CHECK(!proj_get_voice_config(voice, &after) && after.gen.groove == BL_DRUM_STEADY, "GROOVE selects STEADY");
        knob(EN_K4, view_knob(V_PERFORM, 3), 1);
        knob(EN_K4, view_knob(V_PERFORM, 3), 1);
        CHECK(!proj_get_voice_config(voice, &after) && after.gen.groove == BL_DRUM_DRIVE, "GROOVE clamps at DRIVE");
        knob(EN_K4, view_knob(V_PERFORM, 3), -1); /* return to defaults */
        knob(EN_K2, view_knob(V_VOICE, 1), 1);
        knob(EN_K2, KN_GATE, 1);
        knob(EN_K1, KN_DIRECTION, 1);
        knob(EN_K2, KN_GROOVE, 1);
        CHECK(!proj_get_voice_config(voice, &after) && !bl_config_diff(&before, &after),
              "restored drum defaults, hidden Register and fixed Gate preserve configuration");
        CHECK(proj.key_root == root && proj.key_mode == mode, "drum UI preserves global root/scale");
        CHECK(view_knob(V_PATTERN, 1) == KN_NONE && view_knob(V_PATTERN, 2) == KN_NONE,
              "drum ARP hides actual Root/Scale mappings");
        knob(EN_K2, view_knob(V_PATTERN, 1), 1);
        knob(EN_K3, view_knob(V_PATTERN, 2), 1);
        ui.view = V_PATTERN;
        algo_delta = 1;
        ui_input_only();
        CHECK(proj.key_root == root && proj.key_mode == mode && !proj_get_voice_config(voice, &after) &&
              !bl_config_diff(&before, &after), "drum ARP knobs and ALGORITHM preserve global/voice pitch settings");
        ui.view = V_PERFORM;
        if (!live) {
            knob(EN_K3, view_knob(V_PERFORM, 2), -1);
            knob(EN_K4, view_knob(V_PERFORM, 3), 1);
            CHECK(proj_save() == 0, "save drum PARTS/GROOVE");
            proj_defaults();
            CHECK(!proj_load() && !proj_get_voice_config(voice, &after) &&
                  after.gen.direction == BL_DRUM_BD_SD_CH && after.gen.groove == BL_DRUM_DRIVE,
                  "project SAVE/load preserves both drum controls");
        }
        ui.sel = 0;
        CHECK(!heartbeat_selected() && view_knob(V_PERFORM, 2) == KN_VROOT &&
              view_knob(V_PERFORM, 3) == KN_SCALE && view_knob(V_VOICE, 1) == KN_REGISTER &&
              view_knob(V_PATTERN, 1) == KN_VROOT && view_knob(V_PATTERN, 2) == KN_SCALE,
              "melodic voices retain Root/Scale/Register mappings");
        ui.page[V_PERFORM] = 1;
        CHECK(view_knob(V_PERFORM, 2) == KN_DELAY_ON, "HOME PLAY mapping preserved");
    }
    playing = 0;
}

static void test_ui_corrections(void)
{
    playing = 0;
    proj_defaults();
    ui_init();
    ui.view = V_PERFORM;
    ui.page[V_PERFORM] = 2;
    CHECK(!proj_select_composer_stage(3), "establish stored start section");
    int calls = stage_calls;
    algo_delta = 1;
    ui_input_only();
    CHECK(proj_composer_start() == 3 && stage_calls == calls, "COMPOSE OFF ignores ALGORITHM while stopped");
    bl_compcfg_t cfg = proj.composer;
    cfg.enabled = 1;
    CHECK(!proj_set_composer(&cfg), "enable Composer navigation");
    algo_delta = 1;
    ui_input_only();
    CHECK(proj_composer_start() == 4 && stage_calls == calls + 1, "enabling restores stage navigation");
    cfg.enabled = 0;
    CHECK(!proj_set_composer(&cfg), "disable Composer without resetting selection");
    algo_delta = -1;
    ui_input_only();
    CHECK(proj_composer_start() == 4 && stage_calls == calls + 1, "OFF retains selected starting section");
    cfg.enabled = 1;
    CHECK(!proj_set_composer(&cfg), "re-enable Composer");
    playing = 1;
    materialize_loaded();
    algo_delta = 1;
    ui_input_only();
    CHECK(stage_calls == calls + 2, "playing Composer accepts stage navigation");
    knob(EN_K1, KN_COMPOSER, -1);
    int pending = engine.composer.pending_stage;
    algo_delta = -1;
    ui_input_only();
    CHECK(!proj.composer.enabled && stage_calls == calls + 2 && engine.composer.pending_stage == pending,
          "COMPOSE OFF ignores ALGORITHM while playing, including pending targets");

    playing = 0;
    proj_defaults();
    ui_init();
    uint64_t seed = proj.seed;
    preset_delta = -1;
    ui_input_only();
    CHECK(proj.tpl == BL_TPL_DEMO && proj.seed == seed, "first preset clamps without reseeding");
    preset_delta = 1; ui_input_only();
    CHECK(proj.tpl == BL_TPL_FULL, "preset order keeps FULL second");
    preset_delta = 1; ui_input_only();
    CHECK(proj.tpl == BL_TPL_AMBIENT, "preset order keeps AMBIENT last");
    seed = proj.seed;
    preset_delta = 1; ui_input_only();
    CHECK(proj.tpl == BL_TPL_AMBIENT && proj.seed == seed, "last preset clamps without reseeding");
    preset_delta = -1; ui_input_only();
    CHECK(proj.tpl == BL_TPL_FULL, "preset can move back from upper endpoint");

    proj_defaults();
    CHECK(!proj.arr_has, "NONE means no explicit arranger form");
    knob(EN_K2, KN_FORM, 1);
    CHECK(proj_form() == FORM_CLASSIC, "FORM works while stopped even from NONE");
    playing = 1;
    knob(EN_K2, KN_FORM, 1);
    CHECK(proj_form() == FORM_CLASSIC && !strcmp(ui.msg[1], "WHEN STOPPED"), "live FORM edit retains existing prerequisite");
    playing = 0;
}

int main(void)
{
    device_settings_init();
    CHECK(!device_settings_apply(), "apply global clock settings");
    test_settings();
    test_algorithm();
    test_drum_controls();
    test_ui_corrections();
    printf(fails ? "scale app: %d FAILED\n" : "scale app: ok (settings, storage, ALGORITHM, drum controls)\n", fails);
    return fails != 0;
}
