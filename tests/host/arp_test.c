/* SPDX-License-Identifier: GPL-2.0-or-later */
/* TGM-001B: ARP factory, independent phase loops and live lifecycle. */
#include <limits.h>
#include <string.h>
#include "gen.h"
#include "live.h"
#include "state.h"
#include "state_same.h"
#include "test.h"

static bl_key_t key(int mode, int root)
{
    bl_key_t k = {root, {0}};
    bl_scale_preset(mode, &k.scale);
    return k;
}

/* Compare sounding material, independent of seed and lifecycle provenance. */
static int material_same(const bl_pattern_t *a, const bl_pattern_t *b)
{
    if (a->length != b->length || a->start_offset != b->start_offset || a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) {
        const bl_pnote_t *x = &a->note[i], *y = &b->note[i];
        if (x->position != y->position || x->gate != y->gate || x->pitch.degree != y->pitch.degree ||
            x->pitch.octave != y->pitch.octave || x->pitch.accidental != y->pitch.accidental ||
            x->velocity != y->velocity || x->accent != y->accent || x->ratchet != y->ratchet) return 0;
    }
    return 1;
}

static void test_scales(void)
{
    /* Expected semitone roles; includes lower-tone ties and pentatonic substitutes. */
    static const int8_t CHORD[23][4] = {
        {0,3,7,10}, {0,3,7,10}, {0,3,7,10}, {0,4,7,10}, {0,4,7,11},
        {0,4,7,10}, {0,3,7,11}, {0,3,7,10}, {0,3,6,10}, {0,4,7,11},
        {0,4,7,9}, {0,2,7,10}, {0,3,7,8}, {0,3,7,11}, {0,4,7,11},
        {0,3,6,10}, {0,3,7,11}, {0,3,7,10}, {0,4,7,11}, {0,4,7,11},
        {0,4,7,11}, {0,3,7,11}, {0,4,6,10},
    };
    const int ids[] = {BL_GEN_SEED_PROCEDURE, BL_GEN_PULSING_BASS, BL_GEN_STAGED_PLUCK, BL_GEN_BASS_MELODY,
                       BL_GEN_CHORD_LAYER, BL_GEN_HEARTBEAT, BL_GEN_DRONE, BL_GEN_ARP};
    CHECK(BL_NMODES == 23 && BL_NGENS == 8, "scale and generator counts");
    for (int i = 0; i < 8; i++) CHECK(ids[i] == i, "generator ID %d unchanged/appended", i);
    for (int m = 0; m < 23; m++) {
        for (int root = 0; root < 12; root++) {
            bl_key_t k = key(m, root);
            for (int rate = 240; rate <= 480; rate += 240) {
                bl_gencfg_t cfg = {BL_GEN_ARP, (int16_t)rate, 0};
                bl_pattern_t a, b;
                CHECK(bl_make_pattern(&cfg, BL_ROLE_BED1, &k, 1, &a) == 0, "dispatch");
                CHECK(bl_make_pattern(&cfg, BL_ROLE_BED2, &k, UINT64_MAX, &b) == 0 && material_same(&a, &b),
                      "deterministic sequence independent of role/seed %d", m);
                CHECK(a.generator == BL_GEN_ARP && a.seed == 1 && !a.locked && a.remaining == 4,
                      "ARP identification and lifecycle");
                CHECK(a.length == 3840 && a.n == 3840 / rate && a.start_offset == 0, "rate independent of length");
                for (int i = 0; i < a.n; i++) {
                    const bl_pnote_t *n = &a.note[i];
                    int cycle = i % 8;
                    int semitone = bl_resolve(&k, n->pitch, 60) - 60 - root;
                    CHECK(semitone == CHORD[m][cycle % 4] + 12 * (cycle / 4), "chord/UP %d/%d/%d", m, root, i);
                    CHECK(n->position == i * rate && n->gate == rate / 2 && n->velocity == 100 &&
                          !n->accent && n->ratchet == 1 && !n->pitch.accidental, "straight rhythm/gate");
                    CHECK(n->pitch.degree >= 0 && n->pitch.degree < 2 * k.scale.count, "degree octave continuation");
                    if (i < 4 && i) CHECK(n->pitch.degree > a.note[i - 1].pitch.degree, "unique ascending chord");
                }
            }
        }
    }
}

static void test_config_capacity(void)
{
    bl_key_t k = key(BL_MODE_NATURAL_MINOR, 9);
    const int rates[] = {0, -1, 1, 8, 16, 240, 480, INT16_MAX, INT16_MIN};
    const int gates[] = {0, -1, 1, 119, 120, 239, 240, 479, 480, INT16_MAX, INT16_MIN};
    for (unsigned r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        int spacing = rates[r] == 240 ? 240 : 480;
        for (unsigned g = 0; g < sizeof gates / sizeof gates[0]; g++) {
            bl_gencfg_t cfg = {BL_GEN_ARP, (int16_t)rates[r], (int16_t)gates[g]};
            bl_pattern_t p;
            bl_make_pattern(&cfg, BL_ROLE_BED1, &k, 5, &p);
            int gate = gates[g] > 0 && gates[g] < spacing ? gates[g] : spacing / 2;
            CHECK(p.n == 3840 / spacing && p.length == 3840, "invalid rate fallback");
            for (int i = 0; i < p.n; i++)
                CHECK(p.note[i].gate == gate && gate > 0 && gate < spacing, "independent/invalid gate fallback");
        }
    }
    for (int rate = 240; rate <= 480; rate += 240) {
        bl_gencfg_t cfg = {BL_GEN_ARP, (int16_t)rate, 73};
        const bl_tick_t lengths[] = {240, 3600, 3840, 4080, (BL_MAX_NOTES - 1) * rate,
            BL_MAX_NOTES * rate, BL_MAX_NOTES * rate + 1, (BL_MAX_NOTES + 1) * rate,
            INT32_MAX, INT64_MAX, 0, -1, 239};
        for (unsigned j = 0; j < sizeof lengths / sizeof lengths[0]; j++) {
            struct { uint64_t before; bl_pattern_t p; uint64_t after; } guard;
            guard.before = guard.after = UINT64_C(0x123456789abcdef0);
            bl_tick_t length = lengths[j] >= 240 && lengths[j] <= INT32_MAX ? lengths[j] : 3840;
            int slots = (int)((length - 1) / rate + 1), kept = slots < BL_MAX_NOTES ? slots : BL_MAX_NOTES;
            CHECK(bl_make_arp(&cfg, &k, 1, lengths[j], &guard.p) == slots - kept, "omitted note count");
            CHECK(guard.p.n == kept && guard.p.length == length, "capacity and duration");
            CHECK(guard.before == UINT64_C(0x123456789abcdef0) && guard.after == guard.before, "pattern bounds");
            for (int i = 0; i < kept; i++) CHECK(guard.p.note[i].position == i * rate &&
                guard.p.note[i].position < length && guard.p.note[i].gate == 73, "valid positions/capacity");
        }
    }
    bl_gencfg_t cfg = {BL_GEN_ARP, 240, 0};
    bl_pattern_t p;
    bl_make_pattern(&cfg, BL_ROLE_BED1, &k, 1, &p);
    CHECK(bl_with_length(&p, 3600) == 0 && p.n == 15 && p.note[14].position == 3360, "shorten removes notes");
    bl_pattern_t before = p;
    CHECK(bl_with_length(&p, 0) == -1 && material_same(&before, &p), "invalid shortening unchanged");
    const int counts[] = {0, 3, 13, 255};
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        k.scale.count = (uint8_t)counts[i];
        CHECK(bl_make_arp(&cfg, &k, 1, 3840, &p) == 0 && p.n == 0 && p.generator == BL_GEN_ARP, "bad scale count");
    }
    k = key(0, 9);
    k.scale.off[2] = k.scale.off[1];
    CHECK(bl_make_arp(&cfg, &k, 1, 3840, &p) == 0 && p.n == 0, "bad scale ordering");
}

/* Programmatic two-pluck fixture; the production templates are never edited. */
static bl_project_t fixture(int rate, int phase1, int phase2)
{
    bl_project_t p;
    bl_template(BL_TPL_DEMO, &p);
    p.nvoices = 2;
    p.transposition.n = 0;
    p.has_arr = p.nscenes = 0;
    p.has_ratchet = 1;
    p.ratchet_p = F64(0x3fe0000000000000); /* 0.5: must still never mutate ARP. */
    for (int i = 0; i < 2; i++) {
        p.voice[i].gen = (bl_gencfg_t){BL_GEN_ARP, (int16_t)rate, 0};
        p.voice[i].phase_steps = (int8_t)(i ? phase2 : phase1);
        p.voice[i].nlanes = 0;
        p.voice[i].reg_low = 0;
        p.voice[i].reg_high = 127;
        p.voice[i].ref_c = 60;
    }
    return p;
}

static void test_phasing(void)
{
    static bl_live_t e;
    const int phases[][2] = {{0,-1}, {0,0}, {1,-3}, {-15,-2}, {49,127}, {-16,0}};
    for (int rate = 240; rate <= 480; rate += 240) {
        for (unsigned f = 0; f < sizeof phases / sizeof phases[0]; f++) {
            bl_project_t p = fixture(rate, phases[f][0], phases[f][1]);
            bl_live_init(&e, &p);
            bl_pattern_t original[2] = {e.st[0].voice.pat, e.st[1].voice.pat};
            int counts[2] = {0,0};
            for (bl_tick_t lo = 0; lo < 20 * 3840; lo += 240) {
                bl_note_t notes[8];
                int n = bl_live_render(&e, lo, lo + 240, notes, 8);
                CHECK(n >= 0 && n <= 8, "render output bounds");
                for (int j = 0; j < n && j < 8; j++) {
                    bl_note_t *n = &notes[j];
                    int v = n->voice;
                    CHECK(v < 2, "two voices only");
                    if (v >= 2) continue;
                    bl_tick_t local = n->at % original[v].length;
                    int idx = (int)(local / rate);
                    CHECK(local % rate == 0 && idx < original[v].n && n->at >= lo && n->at < lo + 240,
                          "independent loop onsets");
                    CHECK(n->gate == rate / 2 && n->ratchet == 1, "no automatic articulation mutations");
                    CHECK(n->midi == bl_resolve(&p.key, original[v].note[idx].pitch, 60), "pitch cycle repeats locally");
                    counts[v]++;
                }
            }
            for (int v = 0; v < 2; v++) {
                int phase = phases[f][v];
                bl_tick_t length = phase <= -16 ? 3840 : 3840 + phase * 240;
                CHECK(original[v].length == length, "phase_steps length");
                int full = (20 * 3840) / (int)length, rem = (20 * 3840) % (int)length;
                int partial = rem ? (rem - 1) / rate + 1 : 0;
                if (partial > original[v].n) partial = original[v].n;
                CHECK(counts[v] == full * original[v].n + partial, "loop repetition count");
                CHECK(material_same(&original[v], &e.st[v].voice.pat) && !e.st[v].voice.pat.locked &&
                      e.st[v].generation == 1, "stable unlocked ARP across many boundaries");
            }
            if (f == 0 && rate == 480) {
                bl_occ_t at[32];
                int n = bl_occurrences(&original[1], 0, 7200, at, 32);
                CHECK(n == 16 && at[7].at == 3360 && at[8].at == 3600 && at[8].at - at[7].at == 240,
                      "15-step eighth-rate natural boundary interval");
                CHECK(bl_has_onset(&original[0], 3840) && !bl_has_onset(&original[1], 3840) &&
                      bl_has_onset(&original[1], 3600), "independent 16/15 loops");
            }
        }
    }
}

static void test_lifecycle_snapshot(void)
{
    static bl_live_t e;
    bl_project_t p = fixture(240, 0, -1);
    bl_live_init(&e, &p);
    bl_pattern_t before = e.st[0].voice.pat;
    bl_voice_t voice = e.st[0].voice;
    voice.pat.remaining = -1;
    for (int i = 0; i < 100; i++) {
        CHECK(!bl_is_mutable(&voice.pat), "established ARP not automatically mutable");
        CHECK(bl_mutate(&voice, &p.key, (uint64_t)i, 4, p.ratchet_p) == 0 &&
              material_same(&before, &voice.pat), "direct automatic mutate no-op");
    }
    bl_action_t a = {0};
    a.kind = BL_A_LOCK; a.voice = BL_VID_BED1; a.arg = 1;
    CHECK(bl_live_enqueue(&e, &a, 0) == 0, "lock");
    a.kind = BL_A_REGENERATE;
    CHECK(bl_live_enqueue(&e, &a, 1) == 0, "regenerate request while locked");
    bl_note_t notes[8];
    for (int t = 0; t < 4080; t += 240) bl_live_render(&e, t, t + 240, notes, 8);
    CHECK(e.st[0].generation == 1 && e.st[0].voice.pat.locked && material_same(&before, &e.st[0].voice.pat),
          "LOCK suppresses manual regeneration");
    a.kind = BL_A_LOCK; a.arg = 0;
    CHECK(bl_live_enqueue(&e, &a, 4080) == 0, "unlock");
    a.kind = BL_A_REGENERATE;
    CHECK(bl_live_enqueue(&e, &a, 4080) == 0, "manual regenerate unlocked");
    for (int t = 4080; t < 7920; t += 240) bl_live_render(&e, t, t + 240, notes, 8);
    CHECK(e.st[0].generation == 2 && !e.st[0].voice.pat.locked && e.st[0].voice.pat.seed != before.seed &&
          material_same(&before, &e.st[0].voice.pat), "manual regeneration works with deterministic notes");

    static bl_state_t s, back;
    uint8_t buf[8192];
    bl_live_capture(&e, &s);
    int n = state_pack(&s, buf, sizeof buf);
    CHECK(n > 0 && !state_unpack(buf, n, &back) && state_same(&s, &back), "ARP snapshot round trip");
    if (n > 0) {
        buf[7] = BL_NGENS;
        CHECK(state_unpack(buf, n, &back) == -1, "reject generator past ARP");
    }
    s.v[0].pat.generator = BL_NGENS;
    CHECK(state_pack(&s, buf, sizeof buf) == -1, "reject invalid generator on pack");
    s.v[0].pat.generator = BL_GEN_ARP;
    a.kind = BL_A_RECALL; a.state = &s;
    CHECK(bl_live_enqueue(&e, &a, 7920) == 0, "ARP snapshot recall enqueue");
    for (int t = 7920; t < 11760; t += 240) bl_live_render(&e, t, t + 240, notes, 8);
    CHECK(e.st[0].voice.pat.generator == BL_GEN_ARP && material_same(&before, &e.st[0].voice.pat), "recall keeps ARP");

    /* Existing harmony and register folding still process ARP degree pitches. */
    bl_voice_t v = e.st[0].voice;
    v.reg_low = 48; v.reg_high = 72;
    bl_sched_t sched = {1, {{0, 2}}};
    bl_note_t transposed[16];
    int count = bl_render_voice(&v, 0, &sched, &p.key, 0, 3840, transposed, 16);
    CHECK(count == 16, "ARP uses existing harmony pipeline");
    for (int i = 0; i < count && i < 16; i++) {
        bl_pitch_t pitch = v.pat.note[i].pitch;
        pitch.degree += 2;
        CHECK(transposed[i].midi == bl_fold(48, 72, bl_resolve(&p.key, pitch, 60)), "degree transpose and register fold");
    }
}

int main(void)
{
    test_scales();
    test_config_capacity();
    test_phasing();
    test_lifecycle_snapshot();
    printf(fails ? "ARP: %d FAILED\n" : "ARP: ok (scales, rate/gate, capacity, phasing, lifecycle, snapshots)\n", fails);
    return fails != 0;
}
