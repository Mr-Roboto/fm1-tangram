/* SPDX-License-Identifier: GPL-2.0-or-later
 * Focused regression for live Composer form edits, without a second arranger.
 * Host engine only: proves deferred adoption, no engine restart, and safety.
 */
#include "live.h"
#include "test.h"

static bl_live_t e;
static bl_project_t p;
static bl_note_t notes[128];

static void setup(int start)
{
    bl_template(BL_TPL_DEMO, &p);
    p.seed = UINT64_C(0x5eed12345678);
    p.has_arr = 1;
    p.arr = bl_arr_classic;
    p.composer = (bl_compcfg_t){1, 4, 1, 1};
    p.composer_start = (uint8_t)start;
    bl_live_init(&e, &p);
}
static void render_to(bl_tick_t end)
{
    while (e.rendered < end) {
        bl_tick_t next = e.rendered + 240;
        if (next > end) next = end;
        int n = bl_live_render(&e, e.rendered, next, notes, 128);
        CHECK(n <= 128, "render-ahead holds within fixed output capacity");
    }
}
static void test_natural_boundary(void)
{
    setup(1); /* THEME A, eight bars when Composer bars=4 */
    render_to(1);
    bl_tick_t born = e.composer.started_at;
    uint64_t seed = e.project.seed;
    int32_t initial_dwell = e.composer.bars_left;
    CHECK(initial_dwell == 8, "Theme A begins with its normal eight-bar dwell");
    CHECK(!bl_live_set_form(&e, 3), "stage Ambient form during playback");
    CHECK(e.composer.pending_form == 3 && !e.project.arr.ambient,
          "form request does not interrupt the running section");
    render_to(4 * BL_TICKS_PER_BAR + 1);
    CHECK(e.composer.section == 1 && e.composer.pending_form == 3 &&
          !e.project.arr.ambient && e.composer.bars_left == 4,
          "old form continues until the section boundary");
    render_to(8 * BL_TICKS_PER_BAR + 1);
    CHECK(e.composer.section == 2 && e.composer.pending_form == 0 &&
          e.project.arr.ambient && !e.project.arr.reprise,
          "Ambient form commits exactly on Development entry");
    CHECK(e.composer.unit >= 32 && e.composer.bars_left == 8 &&
          e.composer.started_at == born && e.project.seed == seed &&
          e.composer.cycle == 0,
          "new phrase settings without restarting section counters or engine seed");
}
static void test_latest_wins_and_cancellation(void)
{
    setup(1);
    render_to(1);
    CHECK(!bl_live_set_form(&e, 2) && e.composer.pending_form == 2,
          "Slow form requested");
    CHECK(!bl_live_set_form(&e, 3) && e.composer.pending_form == 3,
          "latest form replaces earlier pending choice");
    CHECK(!bl_live_set_form(&e, 1) && e.composer.pending_form == 0,
          "returning to current form cancels pending change");
    render_to(8 * BL_TICKS_PER_BAR + 1);
    CHECK(!e.project.arr.ambient && e.project.arr.min_bars == bl_arr_classic.min_bars &&
          !e.composer.pending_form, "cancelled form is not applied at boundary");
}
static void test_manual_stage_and_cycle(void)
{
    setup(1);
    render_to(1);
    CHECK(!bl_live_set_form(&e, 3), "stage Ambient");
    CHECK(!bl_live_select_composer_stage(&e, 3, e.rendered),
          "schedule explicit Section B entry");
    render_to(BL_TICKS_PER_BAR + 1);
    CHECK(e.composer.section == 3 && e.project.arr.ambient &&
          e.composer.unit >= 32 && !e.composer.pending_form &&
          e.composer.cycle == 0,
          "manual section entry adopts latest form without restarting the cycle");

    setup(6);
    render_to(1);
    CHECK(!bl_live_set_form(&e, 3), "stage Ambient on Outro");
    render_to(4 * BL_TICKS_PER_BAR + 1);
    CHECK(e.composer.section == 0 && e.composer.cycle == 1 &&
          e.project.arr.ambient && e.composer.unit >= 32 &&
          !e.composer.pending_form,
          "Outro to Intro uses the new form on the exact cycle boundary");
}
static void test_guards(void)
{
    setup(1);
    render_to(1);
    CHECK(bl_live_set_form(&e, 0) < 0 && bl_live_set_form(&e, 4) < 0,
          "invalid form IDs rejected");
    e.stop = 0;
    CHECK(bl_live_set_form(&e, 3) < 0, "pending end-of-song rejects form change");
    setup(1);
    e.project.composer.enabled = 0;
    CHECK(bl_live_set_form(&e, 3) < 0, "legacy arranger cannot use Composer-only live edit");
}
int main(void)
{
    test_natural_boundary();
    test_latest_wins_and_cancellation();
    test_manual_stage_and_cycle();
    test_guards();
    if (!fails) puts("composer-live-form: ok");
    return fails ? 1 : 0;
}
