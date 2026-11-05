/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Fresh selected-section entry versus live transitions. Source-only regression
 * coverage: this task does not authorize compiling or executing this file. */
#include "live.h"
#include "test.h"

static bl_live_t engine;
static bl_project_t project;
static bl_note_t notes[128];

/* Chronological order, independent of the legacy BL_PH_ enum order. */
static const int TARGET[7][8] = {
    {70, 0, 45, 65, 0, 100, 0, 40},
    {100, 80, 100, 70, 65, 80, 75, 60},
    {100, 100, 100, 65, 85, 70, 90, 75},
    {60, 100, 100, 80, 100, 60, 85, 65},
    {35, 0, 55, 70, 0, 100, 0, 30},
    {100, 90, 100, 80, 80, 85, 85, 70},
    {45, 0, 35, 60, 0, 100, 0, 25}
};
static const int BARS[7] = {4, 8, 8, 4, 4, 8, 4};

static int expected_level(int section, int role, int tension)
{
    if (section == 3 && (role == BL_ROLE_BED2 || role == BL_ROLE_LEAD)) return 70 + 10 * tension;
    return TARGET[section][role];
}

static void setup(int section, int tension)
{
    bl_template(BL_TPL_FULL, &project);
    project.has_arr = 1;
    project.arr = bl_arr_classic;
    project.density = F64(0x3FF0000000000000);
    project.composer = (bl_compcfg_t){1, 4, 3, (uint8_t)tension};
    project.composer_start = (uint8_t)section;
    bl_live_init(&engine, &project);
}

static void render_to(bl_tick_t end)
{
    (void)bl_live_render(&engine, engine.rendered, end, notes, 128);
}

static void check_profile(int section, int tension)
{
    CHECK(engine.composer.section == section && engine.composer.phase == bl_comp_phase(section),
          "selected section %d and activity phase agree", section);
    for (int role = 0; role < 8; role++)
        CHECK(bl_comp_level(&engine.composer, role) == expected_level(section, role, tension),
              "section %d role %d starts at its own activity, tension %d", section, role, tension);
    for (int i = 0; i < project.nvoices; i++) {
        const bl_voice_t *v = &engine.st[i].voice;
        f64 expected = f64_div(f64_from_i64(expected_level(section, v->role, tension)), f64_from_i64(100));
        CHECK(f64_eq(bl_live_voice_density(&engine, i), expected), "renderer density uses selected section for voice %d", i);
    }
    CHECK(engine.composer.bars_in_section == 0 && engine.composer.bars_left == BARS[section],
          "startup does not fast-forward section age or shorten its dwell");
    CHECK(engine.composer.cycle == 0 && !engine.composer.motion_pending,
          "startup does not fast-forward Phase II or invent a foundation");
}

static void test_selected_starts(void)
{
    for (int section = 1; section < 7; section++) for (int tension = 0; tension <= 3; tension++) {
        setup(section, tension);
        bl_composer_t control;
        bl_comp_init(&control, &project.composer, &project.arr, project.seed);
        int baseline = bl_sched_offset(&project.transposition, 0);
        for (int restart = 0; restart < 2; restart++) {
            if (restart) bl_live_init(&engine, &project); /* same next-Play starting choice */
            check_profile(section, tension);
            CHECK(engine.composer.rng.state == control.rng.state, "selected start consumes no extra Composer RNG");
            render_to(1);
            check_profile(section, tension);
            int lift = section == 3 && tension ? tension + 2 : 0;
            CHECK(bl_live_offset(&engine, 0) == baseline + lift, "fresh B starts at chosen tension, not first ramp step");
            CHECK(!engine.composer.start_pending, "fresh-entry flag is consumed at first boundary");
            CHECK(engine.has_theme == (section == 1), "only actual A entry captures a theme");
            for (int i = 0; i < project.nvoices; i++)
                CHECK(engine.st[i].generation == 1 && engine.st[i].seg_start == 0,
                      "startup neither regenerates material nor shifts voice completion clocks");
            render_to(240);
            check_profile(section, tension);
            CHECK(project.composer_start == section, "repeated initialization preserves the selected start");
        }
    }
}

static void test_intro_and_live_transition(void)
{
    setup(0, 3);
    CHECK(bl_comp_level(&engine.composer, BL_ROLE_BED1) < TARGET[0][BL_ROLE_BED1] &&
          bl_comp_level(&engine.composer, BL_ROLE_LEAD) == 0, "Intro keeps its sparse buildup");

    setup(1, 3);
    render_to(1);
    CHECK(!bl_live_select_composer_stage(&engine, 3, 1), "queue live B selection");
    render_to(BL_TICKS_PER_BAR + 1);
    CHECK(engine.composer.section == 3 && engine.composer.bars_in_section == 0, "live selection binds at next bar");
    CHECK(bl_comp_level(&engine.composer, BL_ROLE_LEAD) == TARGET[1][BL_ROLE_LEAD],
          "live jump keeps previous activity for the staggered lead ramp");
    CHECK(engine.composer.harmonic_step == 1, "live B retains progressive tension entry");

    bl_action_t action = {0};
    action.kind = BL_A_ARRANGER;
    CHECK(!bl_live_enqueue(&engine, &action, engine.rendered), "queue AUTO pause");
    render_to(2 * BL_TICKS_PER_BAR + 1);
    CHECK(!engine.has_arr, "AUTO is paused");
    int age = engine.composer.bars_in_section;
    int level = bl_comp_level(&engine.composer, BL_ROLE_LEAD);
    render_to(3 * BL_TICKS_PER_BAR + 1);
    CHECK(engine.composer.bars_in_section == age && bl_comp_level(&engine.composer, BL_ROLE_LEAD) == level,
          "pause holds the existing ramp state");
    action.arg = 1;
    CHECK(!bl_live_enqueue(&engine, &action, engine.rendered), "queue AUTO resume");
    render_to(4 * BL_TICKS_PER_BAR + 1);
    CHECK(engine.has_arr && engine.composer.section == 3 && !engine.composer.start_pending &&
          engine.composer.bars_in_section == age && bl_comp_level(&engine.composer, BL_ROLE_LEAD) == level,
          "resume does not reinitialize the selected section or its ramp");
}

static void test_controller_enable(void)
{
    setup(3, 2);
    bl_compcfg_t enabled = project.composer;
    project.composer.enabled = 0;
    bl_live_init(&engine, &project);
    uint32_t active = 0;
    for (int i = 0; i < project.nvoices; i++) active |= (uint32_t)engine.st[i].active << i;
    CHECK(!bl_live_set_composer(&engine, &enabled, 0), "enable selected B through controller handover");
    render_to(1);
    CHECK(engine.composer.section == 3 && bl_comp_level(&engine.composer, BL_ROLE_LEAD) == 90 &&
          engine.composer.harmonic_step == 4 && !engine.composer.bars_in_section,
          "controller initialization also uses fresh selected activity/tension without aging");
    for (int i = 0; i < project.nvoices; i++)
        CHECK(engine.st[i].active == (int)(active >> i & 1u), "handover preserves voice activation choices");
}

static void test_protection(void)
{
    setup(3, 3);
    int baseline = bl_sched_offset(&project.transposition, 0);
    bl_action_t action = {0};
    action.kind = BL_A_LOCK;
    action.voice = engine.st[0].vid;
    action.arg = 1;
    CHECK(!bl_live_enqueue(&engine, &action, 0), "lock before fresh entry");
    render_to(1);
    CHECK(engine.st[0].voice.pat.locked && bl_live_offset(&engine, 0) == baseline,
          "LOCK still blocks automatic shared tension");
    CHECK(f64_eq(bl_live_voice_density(&engine, 0), project.density), "locked voice bypasses activity shaping");

    setup(3, 3);
    action.kind = BL_A_TRANSPOSE;
    action.arg = 2;
    CHECK(!bl_live_enqueue(&engine, &action, 0), "manual harmony before fresh entry");
    render_to(1);
    CHECK(bl_live_offset(&engine, 0) == 2 && !engine.composer.harmonic_step,
          "explicit harmony overrides fresh B tension");
}

int main(void)
{
    test_selected_starts();
    test_intro_and_live_transition();
    test_controller_enable();
    test_protection();
    if (!fails) puts("composer selected-section startup: OK");
    return fails ? 1 : 0;
}
