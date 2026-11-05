/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real transport clock/rendering, silent MIDI fixture; no second scheduler. */
#include <string.h>
#include "app.h"
#include "test.h"

static uint32_t now_ms, incoming, note_ons;
static int have_input;
static bl_project_t project;
uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_lock(void) { return 0; }
void plat_unlock(uint32_t flags) { (void)flags; }
int plat_midi_in(uint32_t *packet, int *port)
{
    if (!have_input) return 0;
    *packet = incoming; *port = PORT_USB; have_input = 0;
    return 1;
}
void plat_midi_out(uint32_t packet)
{
    if (((packet >> 8) & 0xF0u) == 0x90u && (packet >> 24)) note_ons++;
}
int plat_store_load(uint32_t object, void *out, uint32_t max)
{ (void)object; (void)out; (void)max; return -1; }
int plat_store_save(uint32_t object, const void *in, uint32_t n)
{ (void)object; (void)in; (void)n; return 0; }
void plat_log(const char *line) { (void)line; }
void proj_ready(void) {} /* this fixture loads its project directly */
#include "../../app/runtime.c"
static void host_sent(bl_tick_t tick, uint32_t packet) { (void)tick; (void)packet; }
#include "../../app/device_settings.c"

static void run_ms(unsigned ms)
{
    for (unsigned i = 0; i < ms; i++) { now_ms++; app_tick(); app_loop(); }
}
static void input(uint8_t status)
{
    incoming = rt_pkt(status, 0, 0); have_input = 1; run_ms(1);
}
static void setup(int section)
{
    have_input = 0;
    rt_init();
    bl_template(BL_TPL_FULL, &project);
    project.bpm = 120;
    project.density = F64(0x0000000000000000);
    project.has_arr = 1;
    project.arr = bl_arr_classic;
    project.composer = (bl_compcfg_t){1, 4, 0, 0};
    project.composer_start = (uint8_t)section;
    rt_load(&project);
}
static void start(void) { rt_play(); run_ms(RT_PREROLL_MS); }
static void same(rt_counters_t a, rt_counters_t b, const char *why)
{
    CHECK(a.seconds == b.seconds && a.part_bars == b.part_bars && a.total_bars == b.total_bars, "%s", why);
}
static unsigned char frozen_state[sizeof rt_count];
static rt_counters_t final_stats;
static void remember_stats(void)
{
    final_stats = rt_counters();
    memcpy(frozen_state, &rt_count, sizeof rt_count);
}
static void still_frozen(void)
{
    same(rt_counters(), final_stats, "stopped statistics remain readable");
    CHECK(!memcmp(frozen_state, &rt_count, sizeof rt_count), "STOP preserves fractional accumulators and pending counter cues");
}
static void wait_section(int section)
{
    unsigned ms = 0;
    while (rt_eng.composer.section != section && ms++ < 4000) run_ms(1);
    CHECK(rt_eng.composer.section == section, "section was rendered within bounded wait");
}

static void starts_and_stops(void)
{
    for (int section = 0; section < 7; section++) {
        setup(section);
        start();
        same(rt_counters(), (rt_counters_t){0,0,0}, "preroll does not count as playback");
        run_ms(2000);
        same(rt_counters(), (rt_counters_t){2,1,1}, "direct section start counts one silent bar");
        remember_stats();
        rt_stop();
        still_frozen();
        CHECK(rt_proj.composer_start == section, "STOP preserves selected starting section");
        CHECK(!rt_select_composer_stage((section + 1) % 7), "change starting section while stopped");
        still_frozen();
        rt_stop();
        still_frozen();
        CHECK(rt_proj.composer_start == (section + 1) % 7, "repeated STOP preserves newly selected starting section");
        run_ms(3000);
        still_frozen();
        start();
        same(rt_counters(), (rt_counters_t){0,0,0}, "new PLAY and preroll start at zero");
        CHECK(rt_eng.composer.section == (section + 1) % 7, "accepted PLAY uses the newly selected section");
        run_ms(2000);
        same(rt_counters(), (rt_counters_t){2,1,1}, "STOP/PLAY starts a fresh performance in the selected section");
        rt_play();
        same(rt_counters(), (rt_counters_t){2,1,1}, "PLAY during playback retains existing no-op behavior");
        rt_stop();
    }
    setup(3); start(); run_ms(1123);
    CHECK(rt_count.ms == 123 && rt_count.total > 0 && rt_count.part > 0, "fixture contains fractional time and bars");
    remember_stats();
    rt_stop(); still_frozen(); run_ms(2000); still_frozen();
    start(); run_ms(500);
    same(rt_counters(), (rt_counters_t){0,0,0}, "old fractions cannot advance a fresh performance");
    CHECK(rt_count.ms == 500 && rt_count.total == 960 && rt_count.part == 960, "only new playback fractions are counted");
    run_ms(1500);
    same(rt_counters(), (rt_counters_t){2,1,1}, "fresh session reaches its first complete bar exactly");
    rt_set_tempo(60); /* existing next-bar tempo boundary */
    bl_tick_t before = rt.tick, total = rt_count.total;
    uint32_t seconds = rt_counters().seconds;
    run_ms(6000);
    CHECK(rt.rate == 60 * 16 && rt_count.total == total + rt.tick - before, "TOTAL follows actual tempo-segment ticks");
    CHECK(rt_counters().seconds == seconds + 6, "TIME is independent of tempo");
    rt_stop();

    setup(3); start(); run_ms(1000);
    rt.stop_at = BL_TICKS_PER_BAR; /* existing scheduled/arranger-stop path */
    run_ms(1100);
    CHECK(!rt.playing && rt_proj.composer_start == 3, "scheduled STOP preserves starting selection");
    same(rt_counters(), (rt_counters_t){2,1,1}, "scheduled STOP retains final complete-bar statistics");
    remember_stats(); run_ms(1000); still_frozen();
}

static void sections_and_lookahead(void)
{
    setup(2); start(); run_ms(2000);
    CHECK(!rt_select_composer_stage(3), "queue B from Development");
    wait_section(3);
    CHECK(rt_count.qn > 0, "future section has a telemetry cue");
    if (!rt_count.qn) return;
    bl_tick_t boundary = rt_count.at[rt_count.qr];
    CHECK(rt.tick < boundary && rt_counters().part_bars == 1, "render-ahead does not reset PART early");
    for (int ms = 0; rt.tick < boundary && ms < 1000; ms++) run_ms(1);
    same(rt_counters(), (rt_counters_t){4,0,2}, "PART resets at sounding B boundary, totals continue");
    run_ms(2000);
    same(rt_counters(), (rt_counters_t){6,1,3}, "B counts its own bars");
    rt_stop();

    setup(2); start(); run_ms(2000);
    CHECK(!rt_select_composer_stage(3), "queue unplayed B");
    wait_section(3);
    uint8_t section = rt_eng.composer.section;
    remember_stats();
    rt_stop(); still_frozen();
    CHECK(rt_eng.composer.section == section && rt_proj.composer_start == 2, "counter reset does not alter Composer state or starting selection");
    run_ms(1000); start();
    same(rt_counters(), (rt_counters_t){0,0,0}, "unplayed section cue cannot leak across STOP/PLAY");
    run_ms(1);
    CHECK(rt_count.section == 2 && rt_count.part == rt.tick && rt_count.part < BL_TICKS_PER_BAR,
          "Development restart counts only fresh sounding ticks");
    rt_stop();

    setup(0); start(); run_ms(8000);
    CHECK(rt_count.section == 1, "automatic Intro to A transition reaches playback");
    same(rt_counters(), (rt_counters_t){8,0,4}, "automatic section transition resets only PART");
    rt_stop();
}

static void project_changes(void)
{
    setup(3); start(); run_ms(2000); remember_stats(); rt_stop();
    still_frozen();
    project.key.root = 4;
    rt_load(&project);
    still_frozen(); /* stopped preview/parameter rebuild retains final statistics */
    project.seed++;
    rt_load(&project);
    still_frozen();
    start(); run_ms(2000);
    project.seed++;
    rt_load(&project);
    unsigned ms = 0;
    while (rt_proj.seed != project.seed && ms++ < 4000) run_ms(1);
    CHECK(rt_proj.seed == project.seed && rt.tick < rt.origin, "new project rendered before sounding takeover");
    CHECK(rt_counters().total_bars >= 1, "project replacement keeps current run statistics");
    for (ms = 0; rt.tick < rt.origin && ms < 1000; ms++) run_ms(1);
    same(rt_counters(), (rt_counters_t){4,2,2}, "project takeover is not a new PLAY and does not reset counters");
    run_ms(2000);
    same(rt_counters(), (rt_counters_t){6,3,3}, "continuous run keeps counting through same-section project replacement");
    project.seed++;
    rt_load(&project);
    ms = 0;
    while (rt_proj.seed != project.seed && ms++ < 4000) run_ms(1);
    CHECK(rt.tick < rt.origin, "second project replacement is still in lookahead");
    remember_stats();
    rt_stop();
    still_frozen();
    start(); run_ms(1);
    same(rt_counters(), (rt_counters_t){0,0,0}, "accepted PLAY starts a new project run at zero");
    rt_stop();
    rt_init();
    same(rt_counters(), (rt_counters_t){0,0,0}, "runtime/session initialization clears counters");
}

static void external_clock(void)
{
    setup(3);
    CHECK(!rt_apply_clock_settings(1, DEVICE_CLOCK_BOTH), "select existing external clock mode");
    input(0xFA); /* MIDI Start */
    CHECK(rt.playing, "external Start enters playback");
    for (int pulse = 0; pulse < 97; pulse++) { run_ms(20); input(0xF8); }
    CHECK(rt_counters().total_bars == 1 && rt_count.total == rt.tick, "external pulses and interpolation count exact musical ticks");
    bl_tick_t before = rt.tick, total = rt_count.total;
    uint32_t played_ms = rt_count.seconds * 1000u + rt_count.ms;
    run_ms(600); /* existing freewheel after the missing-pulse hold */
    CHECK(rt_count.total == total + rt.tick - before && rt.tick > before, "counter follows existing external freewheel");
    CHECK(rt_count.seconds * 1000u + rt_count.ms == played_ms + 600, "external TIME uses actual active milliseconds");
    input(0xFC);
    CHECK(rt_counters().seconds >= 2 && rt_counters().total_bars >= 1, "external STOP retains completed performance statistics");
    remember_stats();
    run_ms(2000);
    still_frozen();
    input(0xFB);
    same(rt_counters(), (rt_counters_t){0,0,0}, "external Continue starts a fresh counter performance");
    run_ms(1);
    CHECK(rt_count.total == rt.tick && rt_count.total < total, "external restart has no retained musical ticks");
    rt_stop();
}

int main(void)
{
    starts_and_stops();
    sections_and_lookahead();
    project_changes();
    external_clock();
    CHECK(note_ons == 0, "all accounting scenarios work with zero audible notes");
    return fails ? 1 : 0;
}
