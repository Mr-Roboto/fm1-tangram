/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <string.h>
#include "live.h"
#include "state.h"
#include "rng.h"
#include "test.h"

static bl_live_t engine;
static bl_project_t project;
static bl_voice_t voice;
static bl_note_t notes[128];
static bl_sched_t schedule;
static bl_state_t saved, restored;
static uint8_t packed[8192];
static const int DRUMS[] = {36, 38, 42, 46};

static int drum(int midi)
{
    for (int i = 0; i < 4; i++) if (midi == DRUMS[i]) return i;
    return -1;
}

static void make(uint64_t seed)
{
    bl_key_t key = {0};
    bl_scale_preset(BL_MODE_NATURAL_MINOR, &key.scale);
    bl_gencfg_t cfg = {BL_GEN_HEARTBEAT, 0, 0, 0, 0};
    memset(&voice, 0, sizeof voice);
    CHECK(bl_make_pattern(&cfg, BL_ROLE_RHYTHM, &key, seed, &voice.pat) == 0, "drums fit existing pattern capacity");
    voice.role = BL_ROLE_RHYTHM;
    voice.midi_channel = 9;
}

static void pitch_isolation(void)
{
    static const int OFFSETS[] = {-21, -1, 0, 5, 28};
    for (int seed = 1; seed <= 8; seed++) {
        make((uint64_t)seed);
        int seen = 0, kicks = 0, snares = 0;
        for (int i = 0; i < voice.pat.n; i++) {
            const bl_pnote_t *n = &voice.pat.note[i];
            int instrument = drum(n->pitch.degree);
            CHECK(instrument >= 0, "factory emits only four GM instruments");
            if (instrument >= 0) seen |= 1 << instrument;
            CHECK(n->pitch.octave == 0 && n->pitch.accidental == 0, "absolute drum representation");
            int subdivision = instrument < 2 ? BL_PPQN : n->position % (BL_PPQN / 2) ? BL_PPQN / 4 : BL_PPQN / 2;
            CHECK(n->gate == subdivision / 2, "each drum gate is 50 percent of its rhythmic subdivision");
            if (instrument == 0) { kicks++; CHECK(n->position % BL_PPQN == 0, "quarter-note kick pulse"); }
            if (instrument == 1) { snares++; CHECK(n->position == BL_PPQN || n->position == 3 * BL_PPQN, "backbeat snare"); }
            for (int j = 0; j < i; j++)
                CHECK(!(n->position == voice.pat.note[j].position &&
                    ((instrument == 2 && voice.pat.note[j].pitch.degree == 46) ||
                     (instrument == 3 && voice.pat.note[j].pitch.degree == 42))), "no simultaneous open/closed hats");
        }
        CHECK(seen == 15 && kicks == 4 && snares == 2, "all instruments with stable pulse/backbeat");
        for (int root = 0; root < 12; root++) for (int mode = 0; mode < BL_NMODES; mode++)
            for (int t = 0; t < 5; t++) for (int setting = 0; setting < 2; setting++) {
                bl_key_t key = {0};
                key.root = root;
                bl_scale_preset(mode, &key.scale);
                schedule.n = 1;
                schedule.ev[0] = (bl_tev_t){0, OFFSETS[t]};
                voice.participates = (uint8_t)setting;
                voice.ref_c = setting ? 96 : 12;
                voice.reg_low = setting ? 84 : 12;
                voice.reg_high = setting ? 96 : 24; /* both exclude every drum */
                voice.midi_channel = setting ? 2 : 9;
                int n = bl_render_voice(&voice, 0, &schedule, &key, 0, BL_TICKS_PER_BAR, notes, 128);
                CHECK(n == voice.pat.n, "pitch settings do not change drum event count");
                for (int i = 0; i < n && i < 128; i++) {
                    CHECK(notes[i].midi == voice.pat.note[i].pitch.degree, "exact stored GM note across keys/transpositions/registers");
                    CHECK(notes[i].at == voice.pat.note[i].position && notes[i].gate == voice.pat.note[i].gate,
                          "drum timing and gates survive rendering");
                    CHECK(notes[i].channel == voice.midi_channel, "default and manual MIDI channels respected");
                    CHECK(notes[i].source_onset == notes[i].at + 1, "actual source onset metadata preserved");
                }
            }
    }
}

static void evolution_and_thinning(void)
{
    make(42);
    bl_key_t key = {0};
    bl_scale_preset(BL_MODE_MAJOR, &key.scale);
    for (int generation = 0; generation < 32; generation++) {
        voice.pat.remaining = -1;
        (void)bl_mutate(&voice, &key, (uint64_t)generation + 1, 4, BL_RATCHET_DEFAULT);
        int kicks = 0;
        for (int i = 0; i < voice.pat.n; i++) {
            CHECK(drum(voice.pat.note[i].pitch.degree) >= 0, "mutation cannot retune instruments");
            if (voice.pat.note[i].pitch.degree == 36) {
                kicks++;
                CHECK(voice.pat.note[i].position % BL_PPQN == 0, "mutation preserves kick pulse");
            }
        }
        CHECK(kicks == 4, "mutation preserves four kicks");
        int n = bl_render_voice(&voice, 0, &schedule, &key, 0, BL_TICKS_PER_BAR, notes, 128);
        CHECK(n == voice.pat.n, "ratchets retain original drum event count before runtime expansion");
        for (int i = 0; i < n && i < 128; i++) {
            const bl_pnote_t *src = &voice.pat.note[i];
            bl_tick_t gate = src->ratchet > 1 ? (BL_PPQN / 4 / src->ratchet) / 2 : src->gate;
            CHECK(notes[i].gate == gate && gate > 0, "mutated drum hits retain 50 percent gate and positive Note-Off duration");
            CHECK(notes[i].midi == src->pitch.degree, "ratchets preserve exact instrument MIDI note");
        }
    }
    voice.pat.locked = 1;
    voice.pat.remaining = -1;
    bl_pattern_t before = voice.pat;
    (void)bl_mutate(&voice, &key, 77, 4, BL_RATCHET_DEFAULT);
    CHECK(!memcmp(&before, &voice.pat, sizeof before), "LOCK preserves drum pattern");
    bl_thin_to_roots(&voice.pat);
    CHECK(voice.pat.n == 4 && voice.pat.locked, "arranger thinning keeps kick pulse");
    for (int i = 0; i < voice.pat.n; i++) CHECK(voice.pat.note[i].pitch.degree == 36, "thinning never retunes drums");
}

static void melodic_gates(void)
{
    bl_key_t key = {0};
    bl_scale_preset(BL_MODE_MAJOR, &key.scale);
    bl_gencfg_t cfg = {BL_GEN_ARP, 480, 123, BL_DIR_UP, BL_GROOVE_STRAIGHT};
    memset(&voice, 0, sizeof voice);
    CHECK(!bl_make_pattern(&cfg, BL_ROLE_LEAD, &key, 1, &voice.pat), "melodic gate fixture");
    voice.ref_c = 60;
    voice.reg_low = 48;
    voice.reg_high = 84;
    voice.pat.note[0].ratchet = 3;
    int n = bl_render_voice(&voice, 0, &schedule, &key, 0, BL_TICKS_PER_BAR, notes, 128);
    CHECK(n > 0 && n <= 128, "melodic gate fixture renders");
    for (int i = 0; i < n && i < 128; i++) CHECK(notes[i].gate == 123, "melodic gates remain unchanged including ratchet source");
}

static void drum_ratchet_gates(void)
{
    make(42);
    bl_key_t key = {0};
    bl_scale_preset(BL_MODE_MAJOR, &key.scale);
    for (int r = 2; r <= 3; r++) {
        for (int i = 0; i < voice.pat.n; i++) voice.pat.note[i].ratchet = (uint8_t)r;
        int n = bl_render_voice(&voice, 0, &schedule, &key, 0, BL_TICKS_PER_BAR, notes, 128);
        CHECK(n == voice.pat.n, "ratchet gate fixture renders every instrument");
        for (int i = 0; i < n && i < 128; i++)
            CHECK(notes[i].gate == (r == 2 ? 60 : 40) && notes[i].ratchet == r &&
                  notes[i].midi == voice.pat.note[i].pitch.degree, "exact 50 percent gate for two/three drum retriggers");
    }
}

static int has_hit(const bl_pattern_t *p, bl_tick_t at, int midi)
{
    for (int i = 0; i < p->n; i++)
        if (p->note[i].position == at && p->note[i].pitch.degree == midi) return 1;
    return 0;
}

static void parts_and_groove(void)
{
    bl_key_t key = {0};
    bl_scale_preset(BL_MODE_MAJOR, &key.scale);
    for (int seed = 1; seed <= 8; seed++) {
        bl_pattern_t p[3];
        for (int groove = 0; groove < 3; groove++) {
            bl_gencfg_t cfg = {BL_GEN_HEARTBEAT, 1, 0, BL_DRUM_ALL, (uint8_t)groove};
            CHECK(!bl_make_pattern(&cfg, BL_ROLE_RHYTHM, &key, (uint64_t)seed, &p[groove]), "drum groove fits capacity");
        }
        /* Pre-controls STEADY reference: the original open-hat choice and
         * one-in-four odd-step hat draw. No generated reference file. */
        bl_rng_t rng;
        bl_rng_seed(&rng, (uint64_t)seed);
        int open = bl_rand_index(&rng, 2) ? 14 : 6, expected = 0;
        for (int step = 0; step < 16; step++) {
            bl_tick_t at = step * (BL_PPQN / 4);
            if (!(step % 4)) { expected++; CHECK(has_hit(&p[BL_DRUM_STEADY], at, 36), "STEADY preserves quarter kicks"); }
            if (step == 4 || step == 12) { expected++; CHECK(has_hit(&p[BL_DRUM_STEADY], at, 38), "STEADY preserves backbeat"); }
            if (!(step % 2) || bl_rand_index(&rng, 4) == 0) {
                expected++;
                CHECK(has_hit(&p[BL_DRUM_STEADY], at, step == open ? 46 : 42), "STEADY preserves seeded hats exactly");
            }
        }
        CHECK(p[BL_DRUM_STEADY].n == expected, "default adds/removes no events");
        CHECK(p[BL_DRUM_SPARSE].n == 10 && p[BL_DRUM_DRIVE].n >= p[BL_DRUM_STEADY].n && p[BL_DRUM_DRIVE].n <= 22,
              "SPARSE thins hats, DRIVE remains bounded");
        for (int i = 0; i < p[BL_DRUM_SPARSE].n; i++)
            CHECK(has_hit(&p[BL_DRUM_STEADY], p[BL_DRUM_SPARSE].note[i].position,
                          p[BL_DRUM_SPARSE].note[i].pitch.degree), "SPARSE retains established pulse");
        for (int i = 0; i < p[BL_DRUM_STEADY].n; i++)
            CHECK(has_hit(&p[BL_DRUM_DRIVE], p[BL_DRUM_STEADY].note[i].position,
                          p[BL_DRUM_STEADY].note[i].pitch.degree), "DRIVE preserves STEADY material");
        for (int parts = 1; parts <= 4; parts++) for (int groove = 0; groove < 3; groove++) {
            bl_gencfg_t cfg = {BL_GEN_HEARTBEAT, 1, 0, (uint8_t)(parts % 4), (uint8_t)groove};
            CHECK(!bl_make_pattern(&cfg, BL_ROLE_RHYTHM, &key, (uint64_t)seed, &voice.pat), "make permitted drum material");
            int kicks = 0;
            for (int i = 0; i < voice.pat.n; i++) {
                int instrument = drum(voice.pat.note[i].pitch.degree);
                CHECK(instrument >= 0 && instrument < parts, "factory obeys strict instrument count %d", parts);
                kicks += instrument == 0;
            }
            CHECK(kicks == 4, "every PARTS/GROOVE choice preserves kick pulse");
            /* Deliberately keep ALL material in the source: output permission
             * must still hold after density filtering, not just at generation. */
            voice.config.gen = cfg;
            for (int density = 0; density < 3; density++) {
                f64 level = density == 0 ? F64(0x0000000000000000) :
                            density == 1 ? F64(0x3FE0000000000000) : F64(0x3FF0000000000000);
                bl_density_cap(&p[groove], level, &voice.pat);
                int n = bl_render_voice(&voice, 0, &schedule, &key, 0, BL_TICKS_PER_BAR, notes, 128);
                CHECK(n >= 0 && n <= 128 && (density || n == 0), "density zero silences permitted drums");
                for (int i = 0; i < n && i < 128; i++)
                    CHECK(drum(notes[i].midi) >= 0 && drum(notes[i].midi) < parts,
                          "density cannot re-enable excluded MIDI instruments");
            }
        }
    }
}

static void controls_recall_and_composer(void)
{
    bl_template(BL_TPL_FULL, &project);
    project.voice[0] = project.voice[5];
    project.nvoices = 1;
    project.has_arr = 1;
    project.arr = bl_arr_classic;
    project.composer = (bl_compcfg_t){1, 4, 3, 3};
    project.composer_start = 1;
    bl_live_init(&engine, &project);
    engine.st[0].voice.pat.locked = 1;
    bl_vconfig_t cfg = engine.st[0].voice.pending;
    CHECK(!bl_config_edit(&cfg, BL_CFG_DRUM_PARTS, BL_DRUM_BD_SD) &&
          !bl_config_edit(&cfg, BL_CFG_DRUM_GROOVE, BL_DRUM_DRIVE), "edit drum controls with existing config fields");
    CHECK(!bl_live_set_config(&engine, BL_VID_KICK, &cfg), "queue manual drum controls while locked");
    (void)bl_live_render(&engine, 0, BL_TICKS_PER_BAR, notes, 128);
    CHECK(engine.st[0].voice.pat.locked && engine.st[0].voice.config.gen.direction == BL_DRUM_BD_SD &&
          engine.st[0].voice.config.gen.groove == BL_DRUM_DRIVE, "controls apply at local boundary and preserve LOCK");
    bl_live_capture(&engine, &saved);
    int size = state_pack(&saved, packed, sizeof packed);
    CHECK(size > 0 && !state_unpack(packed, size, &restored), "drum control snapshot round trip");
    CHECK(restored.v[0].config.gen.direction == BL_DRUM_BD_SD && restored.v[0].config.gen.groove == BL_DRUM_DRIVE,
          "existing snapshot record preserves drum controls");
    for (int section = 0; section < 7; section++) {
        CHECK(!bl_live_select_composer_stage(&engine, section, engine.rendered), "queue Composer section");
        bl_tick_t from = engine.rendered;
        int n = bl_live_render(&engine, from, from + 2 * BL_TICKS_PER_BAR, notes, 128);
        CHECK(n >= 0 && n <= 128, "bounded Composer drum output");
        for (int i = 0; i < n && i < 128; i++) CHECK(notes[i].midi == 36 || notes[i].midi == 38, "Composer never re-enables hats");
        CHECK(engine.st[0].voice.config.gen.direction == BL_DRUM_BD_SD, "manual PARTS remains authoritative");
    }
    /* Also exercise Composer ownership without LOCK/manual protection. */
    project.voice[0].has_config = 1;
    project.voice[0].config = cfg;
    bl_live_init(&engine, &project);
    for (int section = 0; section < 7; section++) {
        CHECK(!bl_live_select_composer_stage(&engine, section, engine.rendered), "select owned Composer section");
        bl_tick_t from = engine.rendered;
        int n = bl_live_render(&engine, from, from + 2 * BL_TICKS_PER_BAR, notes, 128);
        CHECK(!engine.composer.manual_voices && !engine.st[0].voice.pat.locked, "Composer owns unlocked drum voice");
        CHECK(n >= 0 && n <= 128, "bounded owned Composer drum output");
        for (int i = 0; i < n && i < 128; i++) CHECK(notes[i].midi == 36 || notes[i].midi == 38, "owned Composer never re-enables hats");
    }
    CHECK(bl_config_edit(&cfg, BL_CFG_DRUM_PARTS, 4) == -1 &&
          bl_config_edit(&cfg, BL_CFG_DRUM_GROOVE, 3) == -1, "reject invalid drum controls");
    CHECK(!bl_config_edit(&cfg, BL_CFG_GENERATOR, BL_GEN_ARP) && cfg.gen.direction == BL_DIR_UP &&
          cfg.gen.groove == BL_GROOVE_STRAIGHT, "drum controls do not become melodic direction/groove");
    CHECK(!bl_config_edit(&cfg, BL_CFG_GENERATOR, BL_GEN_HEARTBEAT) && cfg.gen.direction == BL_DRUM_ALL &&
          cfg.gen.groove == BL_DRUM_STEADY, "selecting Heartbeat restores default sound");
}

static void live_controls(void)
{
    bl_template(BL_TPL_FULL, &project);
    CHECK(project.voice[5].channel == 9, "template defaults to MIDI channel 10");
    project.voice[0] = project.voice[5];
    project.nvoices = 1;
    project.voice[0].channel = 3;
    project.has_arr = 1;
    project.arr = bl_arr_classic;
    project.composer = (bl_compcfg_t){1, 4, 3, 3};
    project.composer_start = 3; /* B: activity plus harmonic tension */
    bl_live_init(&engine, &project);
    bl_action_t a = {0};
    a.kind = BL_A_TENSION;
    a.voice = BL_VID_KICK;
    CHECK(!bl_live_enqueue(&engine, &a, 0), "enqueue drum tension request");
    int budget = engine.tension;
    for (int bar = 0; bar < 4; bar++) {
        int n = bl_live_render(&engine, bar * BL_TICKS_PER_BAR, (bar + 1) * BL_TICKS_PER_BAR, notes, 128);
        CHECK(n > 0 && n <= 128, "Composer B emits bounded drum events");
        for (int i = 0; i < n && i < 128; i++)
            CHECK(drum(notes[i].midi) >= 0 && notes[i].channel == 3, "Composer/tension preserves GM drums and manual channel");
    }
    CHECK(engine.tension == budget, "pitched one-shot on drums is ignored without spending tension");
    a.kind = BL_A_DENSITY;
    a.value = F64(0x0000000000000000);
    CHECK(!bl_live_enqueue(&engine, &a, engine.rendered), "enqueue zero MASTER density");
    bl_tick_t from = engine.rendered;
    CHECK(bl_live_render(&engine, from, from + BL_TICKS_PER_BAR, notes, 128) == 0, "zero MASTER suppresses all drums");
    CHECK(engine.rendered == from + BL_TICKS_PER_BAR, "silent density preserves musical time");
}

int main(void)
{
    pitch_isolation();
    evolution_and_thinning();
    melodic_gates();
    drum_ratchet_gates();
    parts_and_groove();
    controls_recall_and_composer();
    live_controls();
    return fails ? 1 : 0;
}
