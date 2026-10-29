/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared independent MOTIF musical oracles and programmatic two-voice fixture. */
#pragma once
static bl_key_t key(int mode, int root)
{
    bl_key_t k = {root, {0}};
    bl_scale_preset(mode, &k.scale);
    return k;
}

static int same_notes(const bl_pattern_t *a, const bl_pattern_t *b)
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

/* Independent interval oracle, for the nonnegative degree pitches emitted here. */
static int semitone(const bl_key_t *k, int degree)
{
    return 12 * (degree / k->scale.count) + k->scale.off[degree % k->scale.count];
}

static int verify(const bl_key_t *k, const bl_pattern_t *arp, const bl_pattern_t *motif)
{
    CHECK(motif->generator == BL_GEN_MOTIF && !motif->locked && motif->remaining == 4, "MOTIF provenance/lifecycle");
    CHECK(motif->length == arp->length && motif->start_offset == arp->start_offset && motif->n == arp->n,
          "duration, offset and event count unchanged");
    if (motif->n != arp->n) return 0;
    int changes = 0, midpoint = arp->n;
    for (int i = 0; i < arp->n; i++)
        if (arp->note[i].position >= (arp->length + 1) / 2) { midpoint = i; break; }
    for (int i = 0; i < arp->n; i++) {
        const bl_pnote_t *a = &arp->note[i], *m = &motif->note[i];
        int d = m->pitch.degree, changed = d != a->pitch.degree;
        changes += changed;
        CHECK(m->position == a->position && m->gate == a->gate && m->velocity == a->velocity &&
              m->accent == a->accent && m->ratchet == a->ratchet, "only pitch degrees may change");
        CHECK(m->position >= 0 && m->position < motif->length && (!i || m->position > motif->note[i - 1].position),
              "valid unique events");
        CHECK(d >= 0 && d < 2 * k->scale.count && !m->pitch.accidental && !m->pitch.octave,
              "in-scale, two-octave pitch");
        if (!i || i == midpoint) CHECK(!changed, "local root/midpoint anchor %d", i);
        if (!i) CHECK(bl_resolve(k, m->pitch, 60) == 60 + k->root, "root of local harmonic frame");
        if (!changed) continue;
        CHECK(i > 0 && i + 1 < arp->n && arp->n >= 8, "interior substitution only");
        if (i == 0 || i + 1 >= arp->n || arp->n < 8 || d < 0 || d >= 2 * k->scale.count) continue;
        CHECK(abs(d - a->pitch.degree) == 1, "one neighboring scale degree");
        int chord = 0;
        for (int j = 0; j < 4; j++) chord |= d % k->scale.count == arp->note[j].pitch.degree;
        CHECK(!chord, "melodic color outside selected chord");
        int previous = semitone(k, arp->note[i - 1].pitch.degree);
        int next = semitone(k, arp->note[i + 1].pitch.degree);
        int original = semitone(k, a->pitch.degree), candidate = semitone(k, d);
        CHECK(previous < original && original < next && previous < candidate && candidate < next,
              "ascending contour retained");
        CHECK(candidate - previous <= 5 && next - candidate <= 5, "no leap greater than a perfect fourth");
        CHECK(motif->note[i - 1].pitch.degree == arp->note[i - 1].pitch.degree &&
              motif->note[i + 1].pitch.degree == arp->note[i + 1].pitch.degree, "original chord neighbors retained");
    }
    CHECK(changes <= arp->n / 8 && changes * 4 <= arp->n, "conservative variation budget %d/%d", changes, arp->n);
    if (arp->n == 8) CHECK(changes <= 1, "eight onsets: one substitution at most");
    if (arp->n == 16) CHECK(changes <= 2, "sixteen onsets: two substitutions at most");
    return changes;
}

static bl_project_t fixture(int rate, int phase1, int phase2)
{
    bl_project_t p;
    bl_template(BL_TPL_DEMO, &p);
    p.nvoices = 2;
    p.transposition.n = p.has_arr = p.nscenes = 0;
    p.has_ratchet = 1;
    p.ratchet_p = F64(0x3fe0000000000000);
    for (int i = 0; i < 2; i++) {
        p.voice[i].gen = (bl_gencfg_t){BL_GEN_MOTIF,(int16_t)rate,73};
        p.voice[i].phase_steps = (int8_t)(i ? phase2 : phase1);
        p.voice[i].nlanes = 0;
        p.voice[i].ref_c = 60;
        p.voice[i].reg_low = 0;
        p.voice[i].reg_high = 127;
    }
    return p;
}

