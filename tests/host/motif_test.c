/* SPDX-License-Identifier: GPL-2.0-or-later */
/* TGM-002A: bounded melodic color on ARP, local anchors and stable live loops. */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "gen.h"
#include "live.h"
#include "state.h"
#include "state_same.h"
#include "test.h"

#include "motif_helpers.h"

static void test_scales_and_seeds(void)
{
    CHECK(BL_GEN_ARP == 7 && BL_GEN_MOTIF == 8 && BL_NGENS == 9 && BL_NMODES == 23, "appended MOTIF ID");
    int substitutions = 0, patterns = 0;
    for (int mode = 0; mode < 23; mode++) {
        for (int root = 0; root < 12; root++) {
            bl_key_t k = key(mode, root);
            for (int rate = 240; rate <= 480; rate += 240) {
                bl_gencfg_t cfg = {BL_GEN_MOTIF, (int16_t)rate, 73, BL_DIR_UP, BL_GROOVE_STRAIGHT};
                for (int length = 3600; length <= 3840; length += 240) {
                    bl_pattern_t arp;
                    bl_make_arp(&cfg, &k, 123, length, &arp);
                    for (uint64_t seed = 0; seed < 16; seed++) {
                        bl_pattern_t a, b;
                        CHECK(bl_make_motif(&cfg, &k, seed, length, &a) == 0, "no omitted notes");
                        bl_make_motif(&cfg, &k, seed, length, &b);
                        CHECK(same_notes(&a, &b) && a.seed == seed && b.seed == seed, "determinism");
                        substitutions += verify(&k, &arp, &a);
                        if (length == 3840) {
                            bl_make_pattern(&cfg, BL_ROLE_BED1, &k, seed, &b);
                            CHECK(b.generator == BL_GEN_MOTIF && same_notes(&a, &b), "factory dispatch");
                        }
                        patterns++;
                    }
                }
            }
        }
    }
    CHECK(substitutions > 0, "MOTIF actually adds restrained melodic color");
    bl_key_t k = key(BL_MODE_NATURAL_MINOR, 9);
    for (int rate = 240; rate <= 480; rate += 240) {
        bl_gencfg_t cfg = {BL_GEN_MOTIF, (int16_t)rate, 0, BL_DIR_UP, BL_GROOVE_STRAIGHT};
        bl_pattern_t first, p;
        bl_make_pattern(&cfg, BL_ROLE_BED1, &k, 0, &first);
        int varied = 0;
        for (uint64_t seed = 1; seed <= 64; seed++) {
            bl_make_pattern(&cfg, BL_ROLE_BED1, &k, seed, &p);
            varied |= !same_notes(&first, &p);
        }
        CHECK(varied, "different seeds can yield different motifs at rate %d", rate);
    }
    printf("MOTIF grid: %d patterns, %d substitutions; all 23 scales and 12 roots\n", patterns, substitutions);
}

static void test_fallbacks_and_capacity(void)
{
    bl_key_t k = key(0, 9);
    const int rates[] = {240,480,0,-1,16,INT16_MAX};
    const int gates[] = {0,73,1,239,479,480,-1,INT16_MAX};
    for (unsigned r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        for (unsigned g = 0; g < sizeof gates / sizeof gates[0]; g++) {
            bl_gencfg_t cfg = {BL_GEN_MOTIF, (int16_t)rates[r], (int16_t)gates[g], BL_DIR_UP, BL_GROOVE_STRAIGHT};
            const bl_tick_t lengths[] = {240,960,1680,1920,2160,3600,3840,4080,4800,
                64 * 240, 65 * 240, INT32_MAX, 0, -1, INT64_MAX};
            for (unsigned l = 0; l < sizeof lengths / sizeof lengths[0]; l++) {
                struct { uint64_t pre; bl_pattern_t p; uint64_t post; } guard;
                guard.pre = guard.post = UINT64_C(0x123456789abcdef0);
                bl_pattern_t arp;
                int dropped = bl_make_arp(&cfg, &k, UINT64_MAX, lengths[l], &arp);
                CHECK(bl_make_motif(&cfg, &k, UINT64_MAX, lengths[l], &guard.p) == dropped, "capacity return");
                verify(&k, &arp, &guard.p);
                CHECK(guard.pre == guard.post && guard.pre == UINT64_C(0x123456789abcdef0), "output bounds");
                if (arp.n < 8) CHECK(same_notes(&arp, &guard.p), "short motif keeps foundation");
            }
        }
    }
    bl_gencfg_t cfg = {BL_GEN_MOTIF,480,0,BL_DIR_UP,BL_GROOVE_STRAIGHT};
    /* Valid four-tone scale with no non-chord tones: retain every note, no forced fallback. */
    k.scale = (bl_scale_t){0,4,{0,4,7,11}};
    bl_pattern_t arp, m;
    bl_make_arp(&cfg, &k, 0, 3840, &arp);
    for (uint64_t seed = 0; seed < 64; seed++) {
        bl_make_motif(&cfg, &k, seed, 3840, &m);
        CHECK(same_notes(&arp, &m), "no suitable candidate retains chord tones");
    }
    /* Five tones, with a non-chord tone at 1, but every unprotected replacement
     * would break the contour or leave a leap above five semitones. The upper
     * root could take a neighbor, but it is the protected local midpoint. */
    k.scale = (bl_scale_t){0,5,{0,1,2,10,11}};
    bl_make_arp(&cfg, &k, 0, 3840, &arp);
    for (uint64_t seed = 0; seed < 64; seed++) {
        bl_make_motif(&cfg, &k, seed, 3840, &m);
        CHECK(same_notes(&arp, &m), "never force an unsuitable passing tone");
    }
    k.scale.count = 255;
    bl_make_pattern(&cfg, BL_ROLE_BED1, &k, 0, &m);
    CHECK(m.n == 0 && m.generator == BL_GEN_MOTIF, "invalid scale safely empty");
}

static void test_live_and_snapshots(void)
{
    static bl_live_t e;
    const int phases[][2] = {{0,-1}, {0,0}, {1,-3}};
    for (int rate = 240; rate <= 480; rate += 240) {
        for (unsigned f = 0; f < sizeof phases / sizeof phases[0]; f++) {
            bl_project_t p = fixture(rate, phases[f][0], phases[f][1]);
            bl_live_init(&e, &p);
            bl_pattern_t initial[2] = {e.st[0].voice.pat, e.st[1].voice.pat};
            /* TGM-002B evolves unlocked voices after four cycles. Preserve this
             * TGM-002A fixed-material/phasing regression under explicit LOCK. */
            for (int v = 0; v < 2; v++) e.st[v].voice.pat.locked = 1;
            int counts[2] = {0,0};
            for (int v = 0; v < 2; v++) {
                bl_pattern_t expected, arp;
                bl_tick_t length = 3840 + phases[f][v] * 240;
                bl_make_motif(&p.voice[v].gen, &p.key, initial[v].seed, length, &expected);
                bl_make_arp(&p.voice[v].gen, &p.key, initial[v].seed, length, &arp);
                CHECK(same_notes(&initial[v], &expected), "phase length applied before motif generation");
                verify(&p.key, &arp, &initial[v]);
            }
            for (bl_tick_t lo = 0; lo < 20 * 3840; lo += 240) {
                bl_note_t notes[8];
                int n = bl_live_render(&e, lo, lo + 240, notes, 8);
                CHECK(n >= 0 && n <= 8, "render buffer");
                for (int j = 0; j < n && j < 8; j++) {
                    int v = notes[j].voice;
                    CHECK(v < 2, "two voices");
                    if (v >= 2) continue;
                    bl_tick_t local = notes[j].at % initial[v].length;
                    int idx = (int)(local / rate);
                    CHECK(local % rate == 0 && idx < initial[v].n, "independent local grid");
                    if (idx >= initial[v].n) continue;
                    CHECK(notes[j].at >= lo && notes[j].at < lo + 240 && notes[j].gate == 73 &&
                          notes[j].ratchet == 1, "timing and articulation unchanged");
                    CHECK(notes[j].midi == bl_resolve(&p.key, initial[v].note[idx].pitch, 60), "fixed motif repeats");
                    counts[v]++;
                }
            }
            for (int v = 0; v < 2; v++) {
                int rem = (20 * 3840) % (int)initial[v].length;
                int expected = ((20 * 3840) / (int)initial[v].length) * initial[v].n + (rem ? (rem - 1) / rate + 1 : 0);
                CHECK(counts[v] == expected, "independent loop repetition");
                CHECK(same_notes(&initial[v], &e.st[v].voice.pat) && e.st[v].voice.pat.locked && e.st[v].generation == 1,
                      "LOCK preserves the original stable motif");
            }
        }
    }

    bl_project_t p = fixture(240,0,-1);
    bl_live_init(&e, &p);
    bl_pattern_t original = e.st[0].voice.pat;
    bl_voice_t voice = e.st[0].voice;
    voice.pat.remaining = -1;
    for (uint64_t seed = 0; seed < 100; seed++)
        CHECK(!bl_is_mutable(&voice.pat) && bl_mutate(&voice, &p.key, seed, 4, p.ratchet_p) == 0 &&
              same_notes(&voice.pat, &original), "direct unrestricted mutation is a no-op");
    bl_action_t a = {0};
    a.kind = BL_A_LOCK; a.voice = BL_VID_BED1; a.arg = 1;
    CHECK(!bl_live_enqueue(&e, &a, 0), "lock");
    a.kind = BL_A_REGENERATE;
    CHECK(!bl_live_enqueue(&e, &a, 1), "regeneration requested");
    bl_note_t notes[8];
    for (int t = 0; t < 4080; t += 240) bl_live_render(&e,t,t+240,notes,8);
    CHECK(e.st[0].generation == 1 && e.st[0].voice.pat.locked && same_notes(&original, &e.st[0].voice.pat),
          "explicit LOCK blocks regeneration");
    a.kind = BL_A_LOCK; a.arg = 0;
    CHECK(!bl_live_enqueue(&e, &a, 4080), "unlock");
    a.kind = BL_A_REGENERATE;
    CHECK(!bl_live_enqueue(&e, &a, 4080), "manual regeneration");
    for (int t = 4080; t < 7920; t += 240) bl_live_render(&e,t,t+240,notes,8);
    bl_pattern_t expected;
    bl_make_motif(&p.voice[0].gen, &p.key, e.st[0].voice.pat.seed, 3840, &expected);
    CHECK(e.st[0].generation == 2 && !e.st[0].voice.pat.locked && e.st[0].voice.pat.seed != original.seed &&
          same_notes(&e.st[0].voice.pat, &expected), "manual regeneration uses current settings and new seed");

    static bl_state_t s, back;
    uint8_t buf[8192];
    bl_live_capture(&e, &s);
    int n = state_pack(&s,buf,sizeof buf);
    CHECK(n > 0 && !state_unpack(buf,n,&back) && state_same(&s,&back), "MOTIF snapshot round trip");
    if (n > 0) {
        buf[7] = BL_NGENS;
        CHECK(state_unpack(buf,n,&back) == -1, "generator bound on unpack");
    }
    back = s;
    back.v[0].pat.generator = BL_NGENS;
    CHECK(state_pack(&back,buf,sizeof buf) == -1, "generator bound on pack");
    a.kind = BL_A_RECALL; a.state = &s;
    CHECK(!bl_live_enqueue(&e,&a,7920), "snapshot recall (undo uses same action)");
    for (int t = 7920; t < 11760; t += 240) bl_live_render(&e,t,t+240,notes,8);
    bl_pattern_t recalled = e.st[0].voice.pat;
    recalled.start_offset = s.v[0].pat.start_offset; /* absolute origin changes; saved local phase is restored */
    CHECK(e.st[0].voice.pat.generator == BL_GEN_MOTIF && same_notes(&s.v[0].pat,&recalled),
          "recall keeps the saved motif material");
    bl_voice_t folded = e.st[0].voice;
    folded.pat.start_offset = 0; /* standalone harmony oracle uses local onset order */
    folded.reg_low = 48; folded.reg_high = 72;
    bl_sched_t sched = {1,{{0,2}}};
    bl_note_t rendered[16];
    n = bl_render_voice(&folded,0,&sched,&p.key,0,3840,rendered,16);
    CHECK(n == 16, "existing transposition/rendering pipeline");
    for (int i = 0; i < n && i < 16; i++) {
        bl_pitch_t pitch = folded.pat.note[i].pitch;
        pitch.degree += 2;
        CHECK(rendered[i].midi == bl_fold(48,72,bl_resolve(&p.key,pitch,60)), "harmony and register folding");
    }
}

int main(void)
{
    test_scales_and_seeds();
    test_fallbacks_and_capacity();
    test_live_and_snapshots();
    printf(fails ? "MOTIF: %d FAILED\n" : "MOTIF: ok (candidates, anchors, seeds, timing, phasing, lifecycle, snapshots)\n", fails);
    return fails != 0;
}
