/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The engine on the host, one test function per part: f64 against the host's own IEEE binary64 (`double`, here
 * only: the firmware has none), and bl_sin where the CC lanes use it. Compiled with -ffp-contract=off, so the host's
 * expressions round after every operation, as Swift's do. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "test.h"
#include "f64.h"
#include "rng.h"
#include "music.h"
#include "pattern.h"
#include "gen.h"
#include "harmony.h"
#include "legit.h"
#include "arrange.h"

static double dbl(uint64_t b)
{
    double d;
    memcpy(&d, &b, sizeof d);
    return d;
}
static uint64_t bits(double d)
{
    uint64_t b;
    memcpy(&b, &d, sizeof b);
    return b;
}
static f64 to_f64(double d) { return (f64){bits(d)}; }

/* SplitMix64 (the test's own stream; the engine's is rng.c) */
static uint64_t sm_state;
static uint64_t sm_next(void)
{
    uint64_t z = (sm_state += 0x9E3779B97F4A7C15u);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9u;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBu;
    return z ^ (z >> 31);
}

/* ---------------------------------------------------------------- f64 --- */
static const uint64_t EDGE[] = {
    0x0000000000000000u, 0x8000000000000000u,                       /* +-0 */
    0x0000000000000001u, 0x8000000000000001u,                       /* the least subnormal */
    0x000FFFFFFFFFFFFFu, 0x800FFFFFFFFFFFFFu, 0x0008000000000000u,  /* the largest, a middle one */
    0x0010000000000000u,                                            /* the least normal */
    0x3FF0000000000000u, 0xBFF0000000000000u,                       /* +-1 */
    0x3FE0000000000000u, 0xBFE0000000000000u,                       /* +-0.5 */
    0x4340000000000000u,                                            /* 2^53 */
    0x4340000000000001u,                                            /* 2^53 + 2 (2^53 + 1 as an int: below) */
    0x3FB999999999999Au, 0x3FD3333333333333u, 0x3FEB333333333333u,  /* 0.1, 0.3, 0.85 */
    0x01A56E1FC2F8F359u, 0x7E37E43C8800759Cu,                       /* 1e-300, 1e300 */
    0x7FEFFFFFFFFFFFFFu, 0x7FF0000000000000u, 0xFFF0000000000000u,  /* the largest finite, +-inf */
    0x3FF8000000000000u, 0x4002000000000000u, 0xC004000000000000u,  /* 1.5, 2.25, -2.5: ties */
};
#define NEDGE (int)(sizeof EDGE / sizeof EDGE[0])

static int nan_bits(uint64_t b) { return (b & 0x7FF0000000000000u) == 0x7FF0000000000000u && (b << 12) != 0; }

/* one operand: a raw pattern, an edge value, or (to reach the rounding paths) a value near the other's exponent */
static uint64_t operand(uint64_t near)
{
    uint64_t r = sm_next(), k = r & 7u;
    if (k == 0)
        return EDGE[(r >> 8) % NEDGE];
    if (k <= 2) {
        uint64_t e = (near >> 52 & 0x7FFu) + (r >> 8 & 63u) - 31u;  /* within 31 of near's exponent */
        return (r & 0x800FFFFFFFFFFFFFu) | (e & 0x7FFu) << 52;
    }
    return sm_next();
}

static int nfail_op;
static void check_pair(uint64_t a, uint64_t b)
{
    double x = dbl(a), y = dbl(b);
    struct {
        const char *name;
        uint64_t got, want;
    } r[] = {
        {"add", f64_add((f64){a}, (f64){b}).bits, bits(x + y)},
        {"sub", f64_sub((f64){a}, (f64){b}).bits, bits(x - y)},
        {"mul", f64_mul((f64){a}, (f64){b}).bits, bits(x * y)},
        {"div", f64_div((f64){a}, (f64){b}).bits, bits(x / y)},
        {"lt", (uint64_t)f64_lt((f64){a}, (f64){b}), (uint64_t)(x < y)},
        {"le", (uint64_t)f64_le((f64){a}, (f64){b}), (uint64_t)(x <= y)},
        {"eq", (uint64_t)f64_eq((f64){a}, (f64){b}), (uint64_t)(x == y)},
        {"max", f64_max((f64){a}, (f64){b}).bits, bits(y >= x ? y : x)},
        {"min", f64_min((f64){a}, (f64){b}).bits, bits(y < x ? y : x)},
    };
    int i;
    for (i = 0; i < (int)(sizeof r / sizeof r[0]); i++)
        if (r[i].got != r[i].want && nfail_op++ < 20)
            CHECK(0, "%s(%016llx, %016llx) = %016llx, want %016llx", r[i].name, (unsigned long long)a,
                  (unsigned long long)b, (unsigned long long)r[i].got, (unsigned long long)r[i].want);
}

/* the conversions and roundings of one pattern (finite values; to_i64 where Swift's Int(x) does not trap) */
static void check_one(uint64_t a)
{
    double x = dbl(a);
    if (!isfinite(x))
        return;
    if (f64_round((f64){a}).bits != bits(round(x)) && nfail_op++ < 20)
        CHECK(0, "round(%016llx) = %016llx", (unsigned long long)a, (unsigned long long)f64_round((f64){a}).bits);
    if (f64_ceil((f64){a}).bits != bits(ceil(x)) && nfail_op++ < 20)
        CHECK(0, "ceil(%016llx) = %016llx", (unsigned long long)a, (unsigned long long)f64_ceil((f64){a}).bits);
    if (f64_frac1((f64){a}).bits != bits(fmod(x, 1.0)) && nfail_op++ < 20)
        CHECK(0, "frac1(%016llx) = %016llx, want %016llx", (unsigned long long)a,
              (unsigned long long)f64_frac1((f64){a}).bits, (unsigned long long)bits(fmod(x, 1.0)));
    if (x >= -9223372036854775808.0 && x < 9223372036854775808.0 && f64_to_i64((f64){a}) != (int64_t)x &&
        nfail_op++ < 20)
        CHECK(0, "to_i64(%016llx) = %lld", (unsigned long long)a, (long long)f64_to_i64((f64){a}));
}

static void check_int(uint64_t u)
{
    int64_t v = (int64_t)u;
    if (f64_from_i64(v).bits != bits((double)v) && nfail_op++ < 20)
        CHECK(0, "from_i64(%lld) = %016llx", (long long)v, (unsigned long long)f64_from_i64(v).bits);
    if (f64_from_u64(u).bits != bits((double)u) && nfail_op++ < 20)
        CHECK(0, "from_u64(%llu) = %016llx", (unsigned long long)u, (unsigned long long)f64_from_u64(u).bits);
}

static void test_f64_ops(void)
{
    static const uint64_t INTS[] = {0, 1, (uint64_t)-1, 1ull << 53, (1ull << 53) + 1, (1ull << 53) + 3,
                                    (uint64_t)-(int64_t)((1ull << 53) + 1), 0x8000000000000000u,
                                    0x7FFFFFFFFFFFFFFFu, 0xFFFFFFFFFFFFFFFFu, 3840, 61440, 1ull << 63 | 1};
    int i, j, n = 0;
    uint64_t a, b;
    sm_state = 20261026;
    nfail_op = 0;
    for (i = 0; i < NEDGE; i++) {
        for (j = 0; j < NEDGE; j++)
            check_pair(EDGE[i], EDGE[j]);
        check_one(EDGE[i]);
    }
    for (i = 0; i < (int)(sizeof INTS / sizeof INTS[0]); i++)
        check_int(INTS[i]);
    for (i = 0; i < 2000000; i++) {
        a = operand(sm_next());
        b = operand(a);
        n += nan_bits(a) || nan_bits(b);
        check_pair(a, b);
        check_one(a);
        check_one((b & 0x800FFFFFFFFFFFFFu) | (1021u + (a >> 8) % 60u) << 52);  /* 0.25 .. 2^56: the fractions */
        check_int(b >> (a & 63));                     /* every magnitude */
    }
    CHECK(f64_to_i64(to_f64(-2.75)) == -2 && f64_to_i64(to_f64(2.75)) == 2, "to_i64 truncates");
    CHECK(f64_round(to_f64(2.5)).bits == bits(3.0) && f64_round(to_f64(-2.5)).bits == bits(-3.0), "ties away");
    CHECK(F64(0x3FB999999999999A).bits == bits(0.1), "F64() is the pattern");
    printf("  f64: %d pairs with a NaN operand among the 2000000\n", n);
}

/* --------------------------------------------------------------- sine --- */
/* the CC value of a sine lane at phase x, as ModulationEngine.continuous: v01 = max(0, min(1, center + depth / 2 *
 * wave)) (Swift's min(x, y) is y < x ? y : x, max(x, y) is y >= x ? y : x), UInt8((v01 * 127).rounded()) */
static int cc_value(double center, double depth, double wave)
{
    double v = center + depth / 2 * wave, m = v < 1 ? v : 1, v01 = m >= 0 ? m : 0;
    return (int)round(v01 * 127);
}

static void test_sin_cc_level(void)
{
    static const struct {
        double cycle_bars, center, depth;
    } LANE[2] = {{16, 0.6, 0.3}, {32, 0.5, 0.4}};      /* the templates' two lanes, phase 0 */
    const double pi = dbl(0x400921FB54442D18u), phase = 0; /* Double.pi */
    long raw = 0, total = 0, bad = 0;
    int l, ulp_max = 0;
    int64_t t;
    for (l = 0; l < 2; l++) {
        double cycle_ticks = LANE[l].cycle_bars * (double)3840;
        for (t = 0; t < 10000 * 3840; t += 40) {
            double x = fmod((double)t / cycle_ticks + phase, 1), arg, s;
            uint64_t sb;
            int d;
            if (x < 0)
                x += 1;
            arg = 2 * pi * x;
            s = sin(arg);
            sb = bl_sin(to_f64(arg)).bits;
            d = (int)((int64_t)sb - (int64_t)bits(s));
            if (sb != bits(s)) {
                raw++;
                if (abs(d) > ulp_max)
                    ulp_max = abs(d);
            }
            if (cc_value(LANE[l].center, LANE[l].depth, dbl(sb)) != cc_value(LANE[l].center, LANE[l].depth, s) &&
                bad++ < 20)
                CHECK(0, "lane %d tick %lld: x %016llx, bl_sin %016llx, sin %016llx: CC %d, want %d", l,
                      (long long)t, (unsigned long long)bits(x), (unsigned long long)sb,
                      (unsigned long long)bits(s), cc_value(LANE[l].center, LANE[l].depth, dbl(sb)),
                      cc_value(LANE[l].center, LANE[l].depth, s));
            total++;
        }
    }
    CHECK(ulp_max <= 1, "bl_sin off by %d ulp", ulp_max);
    printf("  sine: %ld of %ld raw results differ from the host's sin in the last bit (at most %d ulp), "
           "%ld CC values differ\n", raw, total, ulp_max, bad);
}

/* the whole range bl_sin serves (|x| < 2pi + 1) and its paths: the small, the near multiples of pi/2, the medium */
static void test_sin_range(void)
{
    const double pi = dbl(0x400921FB54442D18u), lim = 2 * pi + 1;
    long raw = 0;
    int i, k, ulp_max = 0;
    sm_state = 7;
    for (i = 0; i < 1000000; i++) {
        double x = ((double)(sm_next() >> 11) / 9007199254740992.0 * 2 - 1) * lim, s;
        uint64_t sb;
        int d;
        if (i % 4 == 1)                                       /* near k pi/2 (k = -4..4): cancellation */
            x = ((int)(sm_next() % 9) - 4) * (pi / 2) + ldexp((double)(int64_t)sm_next(), -63 - (i % 40));
        else if (i % 4 == 2)                                  /* tiny ones */
            x = ldexp((double)(int64_t)sm_next(), -63 - (int)(sm_next() % 1100));
        s = sin(x);
        sb = bl_sin(to_f64(x)).bits;
        d = (int)((int64_t)sb - (int64_t)bits(s));
        if (sb != bits(s)) {
            raw++;
            if (abs(d) > ulp_max)
                ulp_max = abs(d);
        }
    }
    for (k = -4; k <= 4; k++) {                             /* the multiples themselves */
        double x = k * (pi / 2);
        CHECK(fabs(dbl(bl_sin(to_f64(x)).bits) - sin(x)) <= 1e-15, "sin(%d pi/2)", k);
    }
    CHECK(bl_sin(to_f64(0.0)).bits == bits(0.0) && bl_sin(to_f64(-0.0)).bits == bits(-0.0), "sin(+-0) = +-0");
    CHECK(ulp_max <= 1, "bl_sin off by %d ulp", ulp_max);
    printf("  sine: %ld of 1000000 in (-2pi-1, 2pi+1) differ from the host's in the last bit (at most %d ulp)\n",
           raw, ulp_max);
}

/* ---------------------------------------------------------------- rng --- */
/* tests/equiv/rng.ref: every line replayed on rng.c; the file is read whole, a line at a time */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) buf[n] = 0;
    return buf;
}

static void test_rng_vectors(void)
{
    char *text = read_file("tests/equiv/rng.ref");
    CHECK(text != NULL, "tests/equiv/rng.ref: cannot read");
    if (!text) return;
    int lines = 0, values = 0, redraws = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        char *p = line, *end;
        if (*line == '#') continue;
        char word[16];
        int n = 0;
        while (*p && *p != ' ' && n < 15) word[n++] = *p++;
        word[n] = 0;
        uint64_t seed = strtoull(p, &end, 10);
        p = end;
        bl_rng_t r;
        bl_rng_seed(&r, seed);
        lines++;
        if (!strcmp(word, "derive")) {
            uint64_t want = strtoull(p, &end, 10);
            const char *lab[8];
            int nl = 0;
            for (p = end; *p;) {
                while (*p == ' ') *p++ = 0;
                if (*p) lab[nl++] = p;
                while (*p && *p != ' ') p++;
            }
            uint64_t got = 0;
            CHECK(nl >= 1 && nl <= 4, "derive: %d labels", nl);
            if (nl == 1) got = bl_seed_derive(seed, lab[0], (const char *)NULL);
            if (nl == 2) got = bl_seed_derive(seed, lab[0], lab[1], (const char *)NULL);
            if (nl == 3) got = bl_seed_derive(seed, lab[0], lab[1], lab[2], (const char *)NULL);
            if (nl == 4) got = bl_seed_derive(seed, lab[0], lab[1], lab[2], lab[3], (const char *)NULL);
            CHECK(got == want, "derive %llu %s: %llu, want %llu", (unsigned long long)seed, lab[0],
                  (unsigned long long)got, (unsigned long long)want);
            values++;
            continue;
        }
        int64_t lo = 0, hi = 0, count = 0;
        if (!strcmp(word, "in") || !strcmp(word, "upto")) {
            lo = strtoll(p, &end, 10);
            hi = strtoll(end, &end, 10);
            p = end;
        } else if (!strcmp(word, "index")) {
            count = strtoll(p, &end, 10);
            p = end;
        }
        for (int i = 0;; i++) {
            while (*p == ' ') p++;
            if (!*p) break;
            if (!strcmp(word, "next")) {
                uint64_t want = strtoull(p, &end, 10), got = bl_rng_next(&r);
                CHECK(got == want, "next %llu #%d: %llu, want %llu", (unsigned long long)seed, i,
                      (unsigned long long)got, (unsigned long long)want);
            } else if (!strcmp(word, "unit")) {
                uint64_t want = strtoull(p, &end, 16), got = bl_rand_unit(&r).bits;
                CHECK(got == want, "unit %llu #%d: %016llx, want %016llx", (unsigned long long)seed, i,
                      (unsigned long long)got, (unsigned long long)want);
            } else {
                int64_t want = strtoll(p, &end, 10), got = -1;
                uint64_t before = r.state;
                if (!strcmp(word, "in")) got = bl_rand_in(&r, lo, hi);
                else if (!strcmp(word, "upto")) got = bl_rand_upto(&r, lo, hi);
                else if (!strcmp(word, "bool")) got = bl_rand_bool(&r);
                else if (!strcmp(word, "index")) got = bl_rand_index(&r, count);
                else {
                    CHECK(0, "rng.ref: unknown line %s", word);
                    break;
                }
                /* a draw that moved the state by more than one step took Lemire's rejection branch */
                if ((!strcmp(word, "in") || !strcmp(word, "upto")) && r.state - before != UINT64_C(0x9E3779B97F4A7C15))
                    redraws++;
                CHECK(got == want, "%s %llu #%d: %lld, want %lld", word, (unsigned long long)seed, i, (long long)got,
                      (long long)want);
            }
            values++;
            p = end;
        }
    }
    CHECK(lines >= 100 && values > 30000, "rng.ref: only %d lines, %d values", lines, values);
    CHECK(redraws > 1000, "rng.ref: only %d draws took the rejection branch", redraws);
    printf("rng: %d lines, %d values, %d draws with a rejection\n", lines, values, redraws);
    free(text);

    /* String(Int) */
    static const int64_t D[] = {0, 7, -7, 12, -240, 102, 1000000, INT64_MAX, INT64_MIN};
    static const char *const S[] = {"0", "7", "-7", "12", "-240", "102", "1000000", "9223372036854775807",
                                    "-9223372036854775808"};
    for (int i = 0; i < 9; i++) {
        char buf[24];
        bl_dec(D[i], buf);
        CHECK(!strcmp(buf, S[i]), "bl_dec(%lld) = %s, want %s", (long long)D[i], buf, S[i]);
    }
    /* the label spelled as the engine writes it equals the literal */
    char lab[24];
    bl_dec(12, lab);
    CHECK(bl_seed_derive(0xBE1271A9u, "voice", "bass", "mut", lab, (const char *)NULL) ==
              bl_seed_derive(0xBE1271A9u, "voice", "bass", "mut", "12", (const char *)NULL), "dec label");
}

/* scales, keys, degree pitch: the expectations are worked through Key.quantize by hand and agree with the Swift
 * (see the task report) */
static void test_music(void)
{
    static const int8_t OFF[BL_NMODES][7] = {
        {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 10},
        {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 6, 7, 9, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 3, 5, 7, 10},
        {0, 1, 3, 5, 6, 8, 10}, {0, 2, 4, 5, 7, 9, 11}};
    for (int m = 0; m < BL_NMODES; m++) {
        bl_scale_t s;
        bl_scale_preset(m, &s);
        int n = m == BL_MODE_MINOR_PENTATONIC ? 5 : 7;
        CHECK(s.mode == m && s.count == n, "preset %d: mode %d count %d", m, s.mode, s.count);
        for (int i = 0; i < n; i++) CHECK(s.off[i] == OFF[m][i], "preset %d offset %d = %d", m, i, s.off[i]);
    }
    CHECK(!strcmp(BL_MODE_RAW[BL_MODE_NATURAL_MINOR], "naturalMinor") && !strcmp(BL_MODE_RAW[BL_MODE_MAJOR], "major"),
          "mode raw values");

    bl_key_t a = {9, {0}};
    bl_scale_preset(BL_MODE_NATURAL_MINOR, &a.scale);
    static const int32_t RD[4] = {0, 7, -1, 9};
    static const int32_t R60[4] = {69, 81, 67, 84}, R36[4] = {45, 57, 43, 60};
    for (int i = 0; i < 4; i++) {
        bl_pitch_t p = {RD[i], 0, 0};
        CHECK(bl_resolve(&a, p, 60) == R60[i], "resolve degree %d at 60 = %d", RD[i], bl_resolve(&a, p, 60));
        CHECK(bl_resolve(&a, p, 36) == R36[i], "resolve degree %d at 36 = %d", RD[i], bl_resolve(&a, p, 36));
    }
    bl_pitch_t up = {0, 1, 0}, down = {0, -1, 0}, acc = {0, 0, 1};
    CHECK(bl_resolve(&a, up, 60) == 81 && bl_resolve(&a, down, 60) == 57 && bl_resolve(&a, acc, 60) == 70,
          "resolve octave and accidental");

    /* A natural minor, refC 60: tones ... 57 59 60 62 64 65 67 | 69 71 72 ...; ties (61 63 66 68 70) go down */
    static const int32_t Q[13][2] = {{2, -1}, {2, -1}, {3, -1}, {3, -1}, {4, -1}, {5, -1}, {5, -1},
                                     {6, -1}, {6, -1}, {0, 0},  {0, 0},  {1, 0},  {2, 0}};
    for (int i = 0; i < 13; i++) {
        bl_pitch_t p = bl_quantize(&a, 60 + i, 60);
        CHECK(p.degree == Q[i][0] && p.octave == Q[i][1] && p.accidental == 0, "quantize %d = (%d, %d, %d), want (%d, %d)",
              60 + i, p.degree, p.octave, p.accidental, Q[i][0], Q[i][1]);
    }

    CHECK(bl_fold(24, 48, 60) == 48 && bl_fold(55, 79, 40) == 64 && bl_fold(24, 48, 30) == 30 && bl_fold(24, 48, -5) == 31,
          "fold");
    CHECK(bl_floor_mod(-1, 7) == 6 && bl_floor_div(-1, 7) == -1 && bl_floor_mod(8, 7) == 1 && bl_floor_div(-7, 7) == -1 &&
              bl_floor_mod(-7, 7) == 0,
          "floor div/mod");
}

/* ------------------------------------------------------- patterns, generators --- */
/* tests/equiv/gen.ref: each gen and walk line made again by gen.c, each line after it (cap, roots, accents, occ,
 * onset) applied by pattern.c to that pattern */
static const char *const GEN_NAME[] = {"seedProcedure", "pulsingBass", "stagedPluck", "bassMelody", "chordLayer",
                                       "heartbeat", "drone"};
static const char *const ROLE_NAME[] = {"bedPluck1", "bedPluck2", "bass", "pad", "lead", "drone", "rhythm", "aux"};

static int name_index(const char *const *names, int count, const char *w)
{
    for (int i = 0; i < count; i++)
        if (!strcmp(names[i], w)) return i;
    return -1;
}

static char *next_word(char **p)
{
    while (**p == ' ') (*p)++;
    char *w = *p;
    while (**p && **p != ' ') (*p)++;
    if (**p) *(*p)++ = 0;
    return w;
}
static int64_t next_int(char **p) { return strtoll(next_word(p), NULL, 10); }

/* the line's `<n> <note>...` against the pattern's notes; the first mismatch of a line is reported */
static void check_notes(const char *file, const bl_pattern_t *got, char **p, int line, const char *what)
{
    int n = (int)next_int(p);
    CHECK(got->n == n, "%s:%d %s: %d notes, want %d", file, line, what, got->n, n);
    for (int i = 0; i < n && i < got->n; i++) {
        const bl_pnote_t *g = &got->note[i];
        int64_t w[8];
        for (int k = 0; k < 8; k++) w[k] = next_int(p);
        int same = g->position == w[0] && g->gate == w[1] && g->pitch.degree == w[2] && g->pitch.octave == w[3] &&
                   g->pitch.accidental == w[4] && g->velocity == w[5] && g->accent == w[6] && g->ratchet == w[7];
        CHECK(same, "%s:%d %s note %d: %lld %lld %d %d %d %d %d %d, want %lld %lld %lld %lld %lld %lld %lld %lld",
              file, line, what, i, (long long)g->position, (long long)g->gate, g->pitch.degree, g->pitch.octave,
              g->pitch.accidental, g->velocity, g->accent, g->ratchet, (long long)w[0], (long long)w[1],
              (long long)w[2], (long long)w[3], (long long)w[4], (long long)w[5], (long long)w[6], (long long)w[7]);
        if (!same) return;
    }
}

static int remaining_of(const char *w) { return !strcmp(w, "E") ? -1 : atoi(w); }

static void test_gen_vectors(void)
{
    char *text = read_file("tests/equiv/gen.ref");
    CHECK(text != NULL, "tests/equiv/gen.ref: cannot read");
    if (!text) return;
    static bl_pattern_t cur, q;
    static bl_sched_t sched;
    static bl_occ_t occ[1024];
    int line = 0, ops = 0, patterns = 0;
    char *save = text;
    for (char *l = text; l; l = save) {
        char *nl = strchr(l, '\n');
        save = nl ? nl + 1 : NULL;
        if (nl) *nl = 0;
        line++;
        if (!*l || *l == '#') continue;
        char *p = l, *word = next_word(&p);
        if (!strcmp(word, "gen") || !strcmp(word, "walk")) {
            char what[96];
            if (*word == 'g') {
                bl_gencfg_t cfg;
                const char *kind = next_word(&p);
                cfg.kind = (uint8_t)name_index(GEN_NAME, 7, kind);
                cfg.a = (int16_t)next_int(&p);
                cfg.b = (int16_t)next_int(&p);
                const char *role_w = next_word(&p);
                int role = name_index(ROLE_NAME, 8, role_w);
                bl_key_t key = {(int32_t)next_int(&p), {0}};
                bl_scale_preset(BL_MODE_NATURAL_MINOR, &key.scale);
                uint64_t seed = strtoull(next_word(&p), NULL, 10);
                snprintf(what, sizeof what, "%s %d %d %s %d %llu", kind, cfg.a, cfg.b, role_w, key.root,
                         (unsigned long long)seed);
                CHECK(bl_make_pattern(&cfg, role, &key, seed, &cur) == 0, "gen.ref:%d %s: notes dropped", line, what);
                CHECK(cur.seed == seed && cur.generator == cfg.kind, "gen.ref:%d %s: provenance", line, what);
            } else {
                int bars = (int)next_int(&p);
                sched.n = (uint16_t)next_int(&p);
                for (int i = 0; i < sched.n; i++) {
                    sched.ev[i].bar = (int32_t)next_int(&p);
                    sched.ev[i].offset = (int32_t)next_int(&p);
                }
                uint64_t seed = strtoull(next_word(&p), NULL, 10);
                snprintf(what, sizeof what, "walk %d (%d events) %llu", bars, sched.n, (unsigned long long)seed);
                CHECK(bl_make_bass_melody(bars, &sched, seed, &cur) == 0, "gen.ref:%d %s: notes dropped", line, what);
                CHECK(cur.seed == seed && cur.generator == BL_GEN_BASS_MELODY, "gen.ref:%d %s: provenance", line, what);
            }
            int64_t length = next_int(&p);
            CHECK(cur.length == length, "gen.ref:%d %s: length %lld, want %lld", line, what, (long long)cur.length,
                  (long long)length);
            if (*word == 'g') {
                int64_t offset = next_int(&p);
                const char *gen_w = next_word(&p);
                int remaining = remaining_of(next_word(&p));
                CHECK(cur.start_offset == offset && cur.generator == name_index(GEN_NAME, 7, gen_w) &&
                          cur.remaining == remaining && !cur.locked,
                      "gen.ref:%d %s: offset %lld generator %d remaining %d", line, what, (long long)cur.start_offset,
                      cur.generator, cur.remaining);
            } else {
                CHECK(cur.start_offset == 0 && cur.remaining == 4 && !cur.locked, "gen.ref:%d %s: header", line, what);
            }
            check_notes("gen.ref", &cur, &p, line, what);
            patterns++;
        } else if (!strcmp(word, "cap")) {
            f64 d = {strtoull(next_word(&p), NULL, 16)};
            bl_density_cap(&cur, d, &q);
            CHECK(q.length == next_int(&p), "gen.ref:%d cap: length", line);
            check_notes("gen.ref", &q, &p, line, "cap");
            ops++;
        } else if (!strcmp(word, "roots")) {
            q = cur;
            bl_thin_to_roots(&q);
            CHECK(q.length == next_int(&p), "gen.ref:%d roots: length", line);
            CHECK(q.locked == next_int(&p), "gen.ref:%d roots: locked", line);
            CHECK(q.remaining == remaining_of(next_word(&p)), "gen.ref:%d roots: remaining %d", line, q.remaining);
            check_notes("gen.ref", &q, &p, line, "roots");
            ops++;
        } else if (!strcmp(word, "accents")) {
            q = cur;
            bl_move_accents(&q, strtoull(next_word(&p), NULL, 10));
            check_notes("gen.ref", &q, &p, line, "accents");
            ops++;
        } else if (!strcmp(word, "occ") || !strcmp(word, "onset")) {
            /* withLength(newLength).rotated(by: rotate) */
            bl_tick_t len = next_int(&p), rotate = next_int(&p), lo = next_int(&p);
            q = cur;
            CHECK(bl_with_length(&q, len) == 0, "gen.ref:%d: withLength %lld", line, (long long)len);
            q.start_offset = bl_floor_mod(q.start_offset + rotate, q.length);
            if (*word == 'o' && word[1] == 'c') {
                bl_tick_t hi = next_int(&p), offset = next_int(&p);
                int n = (int)next_int(&p), got = bl_occurrences(&q, lo, hi, occ, 1024);
                CHECK(q.start_offset == offset, "gen.ref:%d occ: offset %lld, want %lld", line,
                      (long long)q.start_offset, (long long)offset);
                CHECK(got == n, "gen.ref:%d occ: %d onsets, want %d", line, got, n);
                for (int i = 0; i < n && i < got; i++) {
                    bl_tick_t at = next_int(&p);
                    int index = (int)next_int(&p);
                    CHECK(occ[i].at == at && occ[i].index == index, "gen.ref:%d occ %d: %lld %d, want %lld %d", line, i,
                          (long long)occ[i].at, occ[i].index, (long long)at, index);
                    if (occ[i].at != at || occ[i].index != index) break;
                }
                /* a buffer too small: the total still returned, the first ones of the sorted result written */
                static bl_occ_t part[1024];
                for (int max = 0; max <= n; max += n / 3 + 1) {
                    int ret = bl_occurrences(&q, lo, hi, part, max), same = ret == got;
                    for (int i = 0; i < max && i < got; i++)
                        same &= part[i].at == occ[i].at && part[i].index == occ[i].index;
                    CHECK(same, "gen.ref:%d occ into %d: %d returned, want %d and the prefix", line, max, ret, got);
                }
            } else {
                bl_tick_t step = next_int(&p);
                const char *bits = next_word(&p);
                for (int i = 0; bits[i]; i++) {
                    int got = bl_has_onset(&q, lo + i * step);
                    CHECK(got == bits[i] - '0', "gen.ref:%d onset at %lld: %d", line, (long long)(lo + i * step), got);
                    if (got != bits[i] - '0') break;
                }
            }
            ops++;
        } else {
            CHECK(0, "gen.ref:%d: unknown line %s", line, word);
        }
    }
    CHECK(patterns == 5600 && ops == 756, "gen.ref: %d patterns, %d ops", patterns, ops);
    printf("gen: %d patterns, %d ops\n", patterns, ops);
    free(text);
}

/* the most notes a template's generator makes fits a pattern, none dropped: every template config in both keys with
 * gen.ref's 200 seeds, the bass melody over the demo's schedule, and its worst case (a change of 14 degrees every
 * bar: three passing tones in each of its 16 bars, 64 notes); a config beyond the cap reports what it dropped */
static void test_gen_max_notes(void)
{
    static const bl_gencfg_t CFG[] = {
        {BL_GEN_SEED_PROCEDURE, 1, 4}, {BL_GEN_SEED_PROCEDURE, 1, 3}, {BL_GEN_PULSING_BASS, 3, 0},
        {BL_GEN_STAGED_PLUCK, 2, 0},   {BL_GEN_BASS_MELODY, 16, 0},   {BL_GEN_CHORD_LAYER, 7, 0},
        {BL_GEN_HEARTBEAT, 0, 0},      {BL_GEN_DRONE, 8, 0},
    };
    static const int ROLE[] = {BL_ROLE_BED1, BL_ROLE_BED1, BL_ROLE_BASS, BL_ROLE_LEAD,
                               BL_ROLE_BASS, BL_ROLE_PAD,  BL_ROLE_RHYTHM, BL_ROLE_DRONE};
    static bl_sched_t demo, zigzag;
    static bl_pattern_t p;
    demo.n = 5;
    demo.ev[0] = (bl_tev_t){8, 5};
    demo.ev[1] = (bl_tev_t){12, 3};
    demo.ev[2] = (bl_tev_t){16, 0};
    demo.ev[3] = (bl_tev_t){24, 4};
    demo.ev[4] = (bl_tev_t){28, 0};
    zigzag.n = 16;
    for (int i = 0; i < 16; i++) zigzag.ev[i] = (bl_tev_t){i, i & 1 ? -7 : 7};
    int most = 0, dropped = 0;
    for (int root = 0; root <= 9; root += 9) {
        bl_key_t key = {root, {0}};
        bl_scale_preset(BL_MODE_NATURAL_MINOR, &key.scale);
        for (uint64_t k = 0; k < 200; k++) {
            uint64_t seed = bl_seed_derive(k, "gen", (const char *)NULL);
            for (int c = 0; c < 8; c++) {
                dropped += bl_make_pattern(&CFG[c], ROLE[c], &key, seed, &p);
                if (p.n > most) most = p.n;
            }
            dropped += bl_make_bass_melody(16, &demo, seed, &p);
            if (p.n > most) most = p.n;
        }
    }
    CHECK(most > 0 && most <= BL_MAX_NOTES && dropped == 0, "templates' generators: %d notes, %d dropped", most,
          dropped);
    for (uint64_t seed = 0; seed < 500; seed++) {
        int d = bl_make_bass_melody(16, &zigzag, seed, &p);
        CHECK(p.n == BL_MAX_NOTES && d == 0, "bass melody over a zigzag, seed %llu: %d notes, %d dropped",
              (unsigned long long)seed, p.n, d);
    }
    printf("gen: the templates' generators make at most %d notes, the zigzag bass melody %d\n", most, p.n);
    /* 17 bars of the unmutated seed procedure are 68 notes: 4 dropped, and said so */
    bl_key_t a = {9, {0}};
    bl_scale_preset(BL_MODE_NATURAL_MINOR, &a.scale);
    bl_gencfg_t big = {BL_GEN_SEED_PROCEDURE, 17, 0};
    int d = bl_make_pattern(&big, BL_ROLE_BED1, &a, 1, &p);
    CHECK(d == 4 && p.n == BL_MAX_NOTES, "seedProcedure(17, 0): %d notes, %d dropped", p.n, d);
    /* a kind that is not ported: an empty bar */
    bl_gencfg_t none = {BL_GEN_DRONE + 1, 8, 0};
    CHECK(bl_make_pattern(&none, BL_ROLE_AUX, &a, 1, &p) == 0 && p.n == 0 && p.length == BL_TICKS_PER_BAR,
          "unported kind: %d notes, length %lld", p.n, (long long)p.length);

    /* withLength(0) or less is Swift's trap: the pattern stays as it was */
    bl_gencfg_t drone = {BL_GEN_DRONE, 8, 0};
    bl_make_pattern(&drone, BL_ROLE_DRONE, &a, 1, &p);
    CHECK(bl_with_length(&p, 0) == -1 && bl_with_length(&p, -240) == -1 && p.length == 8 * BL_TICKS_PER_BAR &&
              p.n == 2,
          "withLength <= 0");

    bl_sched_t *s = &zigzag;
    CHECK(bl_sched_offset(s, 0) == 7 && bl_sched_offset(s, 1) == -7 && bl_sched_offset(s, 99) == -7, "sched offset");
    s = &demo;
    CHECK(bl_sched_offset(s, 7) == 0 && bl_sched_offset(s, 8) == 5 && bl_sched_offset(s, 11) == 5 &&
              bl_sched_offset(s, 12) == 3 && bl_sched_offset(s, 100) == 0,
          "sched offset before, at and after events");
    /* firstSoundingNote: the first of the least positions */
    p.n = 3;
    p.note[0].position = 480;
    p.note[1].position = 0;
    p.note[2].position = 0;
    CHECK(bl_first_sounding(&p) == 1, "first sounding");
    p.n = 0;
    CHECK(bl_first_sounding(&p) == -1, "first sounding of none");
}

/* ------------------------------------------------------ legitimization --- */
/* tests/equiv/mut.ref: each voice made again by gen.c, then its lines applied in order to that voice by legit.c
 * (mut: a mutation, advance: loop boundaries, lock: the lock flag); a life line starts a bare lifecycle. The span
 * guard must have refused some degree shifts, or the vectors do not test it */
static const char *const ANCHOR_NAME[] = {"none", "beat1", "beats1and3"};

static void test_mut_vectors(void)
{
    char *text = read_file("tests/equiv/mut.ref");
    CHECK(text != NULL, "tests/equiv/mut.ref: cannot read");
    if (!text) return;
    static bl_voice_t v, before;
    static char id[32];
    bl_key_t key = {0, {0}};
    uint64_t master = 0;
    int line = 0, voices = 0, muts = 0, same = 0, advances = 0, lives = 0, refused = 0;
    char *save = text, what[128] = "";
    for (char *l = text; l; l = save) {
        char *nl = strchr(l, '\n');
        save = nl ? nl + 1 : NULL;
        if (nl) *nl = 0;
        line++;
        if (!*l || *l == '#') continue;
        char *p = l, *word = next_word(&p);
        if (!strcmp(word, "voice")) {
            snprintf(id, sizeof id, "%s", next_word(&p));
            v.id = id;
            v.role = (uint8_t)name_index(ROLE_NAME, 8, next_word(&p));
            v.anchor = (uint8_t)name_index(ANCHOR_NAME, 3, next_word(&p));
            v.reg_low = (uint8_t)next_int(&p);
            v.reg_high = (uint8_t)next_int(&p);
            v.ref_c = (int32_t)next_int(&p);
            v.participates = (uint8_t)next_int(&p);
            key.root = (int32_t)next_int(&p);
            const char *mode = next_word(&p);
            bl_scale_preset(name_index(BL_MODE_RAW, BL_NMODES, mode), &key.scale);
            bl_gencfg_t cfg;
            const char *kind = next_word(&p);
            cfg.kind = (uint8_t)name_index(GEN_NAME, 7, kind);
            cfg.a = (int16_t)next_int(&p);
            cfg.b = (int16_t)next_int(&p);
            master = strtoull(next_word(&p), NULL, 10);
            bl_tick_t phase = next_int(&p);
            snprintf(what, sizeof what, "%s %s %d %s %s %d %d %llu", id, ANCHOR_NAME[v.anchor], key.root, mode, kind,
                     cfg.a, cfg.b, (unsigned long long)master);
            CHECK(bl_make_pattern(&cfg, v.role, &key, master, &v.pat) == 0, "mut.ref:%d %s: notes dropped", line,
                  what);
            if (phase) CHECK(bl_with_length(&v.pat, v.pat.length + phase) == 0, "mut.ref:%d %s: phase", line, what);
            bl_tick_t length = next_int(&p), offset = next_int(&p);
            int locked = (int)next_int(&p), remaining = remaining_of(next_word(&p));
            CHECK(v.pat.length == length && v.pat.start_offset == offset && v.pat.locked == locked &&
                      v.pat.remaining == remaining,
                  "mut.ref:%d %s: length %lld offset %lld locked %d remaining %d", line, what, (long long)v.pat.length,
                  (long long)v.pat.start_offset, v.pat.locked, v.pat.remaining);
            check_notes("mut.ref", &v.pat, &p, line, what);
            voices++;
        } else if (!strcmp(word, "mut")) {
            int gen = (int)next_int(&p);
            uint64_t seed = strtoull(next_word(&p), NULL, 10);
            int repeats = (int)next_int(&p);
            const char *rw = next_word(&p);
            f64 ratchet = strcmp(rw, "nil") ? (f64){strtoull(rw, NULL, 16)} : BL_RATCHET_DEFAULT;
            char g[24];
            bl_dec(gen, g);
            CHECK(bl_seed_derive(master, "voice", v.id, "mut", g, (const char *)NULL) == seed,
                  "mut.ref:%d %s: the seed of mutation %d", line, what, gen);
            before = v;
            int may = bl_is_mutable(&v.pat);
            refused += bl_mutate(&v, &key, seed, repeats, ratchet);
            CHECK(v.id == before.id && v.role == before.role && v.anchor == before.anchor &&
                      v.reg_low == before.reg_low && v.reg_high == before.reg_high && v.ref_c == before.ref_c &&
                      v.participates == before.participates && v.pat.seed == before.pat.seed &&
                      v.pat.generator == before.pat.generator,
                  "mut.ref:%d %s: the voice changed", line, what);
            /* the identity leaves the pattern as it was, to the byte */
            if (!may) {
                CHECK(!memcmp(&v.pat, &before.pat, sizeof v.pat), "mut.ref:%d %s: not the identity", line, what);
                same++;
            }
            int locked = (int)next_int(&p), remaining = remaining_of(next_word(&p));
            bl_tick_t length = next_int(&p), offset = next_int(&p);
            CHECK(v.pat.locked == locked && v.pat.remaining == remaining && v.pat.length == length &&
                      v.pat.start_offset == offset,
                  "mut.ref:%d %s mut %d: locked %d remaining %d length %lld offset %lld", line, what, gen,
                  v.pat.locked, v.pat.remaining, (long long)v.pat.length, (long long)v.pat.start_offset);
            char w2[160];
            snprintf(w2, sizeof w2, "%s mut %d", what, gen);
            check_notes("mut.ref", &v.pat, &p, line, w2);
            muts++;
        } else if (!strcmp(word, "advance")) {
            int n = (int)next_int(&p);
            for (int i = 0; i < n; i++) {
                bl_advance_boundary(&v.pat);
                int remaining = remaining_of(next_word(&p)), may = (int)next_int(&p);
                CHECK(v.pat.remaining == remaining && bl_is_mutable(&v.pat) == may,
                      "mut.ref:%d %s: boundary %d: remaining %d mutable %d, want %d %d", line, what, i,
                      v.pat.remaining, bl_is_mutable(&v.pat), remaining, may);
            }
            advances++;
        } else if (!strcmp(word, "lock")) {
            v.pat.locked = (uint8_t)next_int(&p);
        } else if (!strcmp(word, "life")) {
            v.pat.remaining = (int16_t)remaining_of(next_word(&p));
            v.pat.locked = (uint8_t)next_int(&p);
            snprintf(what, sizeof what, "life %d %d", v.pat.remaining, v.pat.locked);
            CHECK(bl_is_mutable(&v.pat) == next_int(&p), "mut.ref:%d %s: mutable", line, what);
            lives++;
        } else {
            CHECK(0, "mut.ref:%d: unknown line %s", line, word);
        }
    }
    CHECK(voices == 300 && muts == 3060 && same == 60 && advances == 3020 && lives == 20,
          "mut.ref: %d voices, %d mutations (%d the identity), %d advance lines, %d lifecycles", voices, muts, same,
          advances, lives);
    CHECK(refused > 0, "mut.ref: the span guard never refused a degree shift");
    printf("mut: %d voices, %d mutations (%d the identity), %d degree shifts refused by the span guard\n", voices,
           muts, same, refused);
    free(text);
}

/* the anchor rule's ticks, and the lifecycle at its edges (remaining 1, 0, established) */
static void test_legit_lifecycle(void)
{
    bl_tick_t t[2];
    CHECK(bl_anchor_ticks(BL_ANCHOR_NONE, t) == 0, "anchors of none");
    CHECK(bl_anchor_ticks(BL_ANCHOR_BEAT1, t) == 1 && t[0] == 0, "anchors of beat1");
    CHECK(bl_anchor_ticks(BL_ANCHOR_BEATS13, t) == 2 && t[0] == 0 && t[1] == 1920, "anchors of beats1and3");
    static bl_pattern_t p;
    p.remaining = 2;
    bl_advance_boundary(&p);
    CHECK(p.remaining == 1 && !bl_is_mutable(&p), "2 -> 1");
    bl_advance_boundary(&p);
    CHECK(p.remaining == -1 && bl_is_mutable(&p), "1 -> established");
    bl_advance_boundary(&p);
    CHECK(p.remaining == -1 && bl_is_mutable(&p), "established stays");
    p.locked = 1;
    CHECK(!bl_is_mutable(&p), "locked");
}

/* ---------------------------------------------------------------- harmony --- */
/* tests/equiv/harmony.ref: each sched line merged in two parts (old + add, split where the line's index says) and
 * queried; each bar and vel line; each voice rendered in every window of its window lines, the windows no w line
 * names being empty, and every note's velocity through bl_velocity at its loop position */
#define HARMONY_TOTAL (64 * BL_TICKS_PER_BAR)

static int harmony_line, harmony_windows, harmony_notes;

/* the voice's windows from *cursor up to end, each of them empty */
static void harmony_empty(const bl_voice_t *v, uint8_t index, const bl_sched_t *s, const bl_key_t *key,
                          bl_tick_t *cursor, bl_tick_t window, bl_tick_t end, const char *what)
{
    static bl_note_t out[1];
    for (; *cursor < end; *cursor += window) {
        bl_tick_t hi = *cursor + window < HARMONY_TOTAL ? *cursor + window : HARMONY_TOTAL;
        int n = bl_render_voice(v, index, s, key, *cursor, hi, out, 1);
        CHECK(n == 0, "harmony.ref:%d %s: window %lld..<%lld has %d notes, want none", harmony_line, what,
              (long long)*cursor, (long long)hi, n);
        harmony_windows++;
    }
}

static void test_harmony_vectors(void)
{
    char *text = read_file("tests/equiv/harmony.ref");
    CHECK(text != NULL, "tests/equiv/harmony.ref: cannot read");
    if (!text) return;
    static bl_sched_t s, sched;
    static bl_voice_t v;
    static bl_tev_t ev[64];
    static bl_note_t out[256], one[2];
    static char id[32];
    bl_key_t key = {0, {0}};
    bl_tick_t window = 0, cursor = HARMONY_TOTAL;
    uint8_t index = 0;
    int scheds = 0, bars = 0, vels = 0, voices = 0, clamped = 0;
    char *save = text, what[96] = "";
    harmony_line = harmony_windows = harmony_notes = 0;
    for (char *l = text; l; l = save) {
        char *nl = strchr(l, '\n');
        save = nl ? nl + 1 : NULL;
        if (nl) *nl = 0;
        harmony_line++;
        int line = harmony_line;
        if (!*l || *l == '#') continue;
        char *p = l, *word = next_word(&p);
        /* a line other than w ends the windows of the voice before */
        if (strcmp(word, "w")) harmony_empty(&v, index, &sched, &key, &cursor, window, HARMONY_TOTAL, what);
        if (!strcmp(word, "sched")) {
            int n = (int)next_int(&p), k = scheds % (n + 1);
            for (int i = 0; i < n; i++) {
                ev[i].bar = (int32_t)next_int(&p);
                ev[i].offset = (int32_t)next_int(&p);
            }
            s.n = 0;
            CHECK(bl_sched_merge(&s, ev, k, 0) == 0 && bl_sched_merge(&s, ev + k, n - k, 0) == 0,
                  "harmony.ref:%d: merge refused", line);
            int m = (int)next_int(&p);
            CHECK(s.n == m, "harmony.ref:%d: %d events, want %d", line, s.n, m);
            for (int i = 0; i < m; i++) {
                int32_t bar = (int32_t)next_int(&p), off = (int32_t)next_int(&p);
                CHECK(i >= s.n || (s.ev[i].bar == bar && s.ev[i].offset == off),
                      "harmony.ref:%d: event %d %d %d, want %d %d", line, i, s.ev[i].bar, s.ev[i].offset, bar, off);
            }
            for (int32_t bar = 0; bar <= 200; bar++) {
                int32_t want = (int32_t)next_int(&p), got = bl_sched_offset(&s, bar);
                CHECK(got == want, "harmony.ref:%d: offset at bar %d %d, want %d", line, bar, got, want);
            }
            scheds++;
        } else if (!strcmp(word, "bar")) {
            bl_tick_t tick = next_int(&p);
            int32_t want = (int32_t)next_int(&p), got = bl_quantized_bar(tick);
            CHECK(got == want, "harmony.ref:%d: quantized bar of %lld %d, want %d", line, (long long)tick, got, want);
            bars++;
        } else if (!strcmp(word, "vel")) {
            int vel = (int)next_int(&p), accent = (int)next_int(&p);
            uint64_t seed = strtoull(next_word(&p), NULL, 10);
            bl_tick_t local = next_int(&p);
            int want = (int)next_int(&p), got = bl_velocity((uint8_t)vel, accent, seed, local);
            CHECK(got == want, "harmony.ref:%d: velocity %d %d %llu %lld: %d, want %d", line, vel, accent,
                  (unsigned long long)seed, (long long)local, got, want);
            if (want == 1 || want == 127) clamped++;
            vels++;
        } else if (!strcmp(word, "schedule")) {
            next_word(&p);
            int n = (int)next_int(&p);
            sched.n = 0;
            for (int i = 0; i < n; i++) {
                bl_tev_t e;
                e.bar = (int32_t)next_int(&p);
                e.offset = (int32_t)next_int(&p);
                CHECK(bl_sched_merge(&sched, &e, 1, 0) == 0, "harmony.ref:%d: merge refused", line);
            }
            CHECK(sched.n == n, "harmony.ref:%d: %d events, want %d", line, sched.n, n);
        } else if (!strcmp(word, "key")) {
            key.root = (int32_t)next_int(&p);
            bl_scale_preset(name_index(BL_MODE_RAW, BL_NMODES, next_word(&p)), &key.scale);
        } else if (!strcmp(word, "voice")) {
            index = (uint8_t)next_int(&p);
            snprintf(id, sizeof id, "%s", next_word(&p));
            v.id = id;
            v.reg_low = (uint8_t)next_int(&p);
            v.reg_high = (uint8_t)next_int(&p);
            v.ref_c = (int32_t)next_int(&p);
            v.participates = (uint8_t)next_int(&p);
            v.pat.seed = strtoull(next_word(&p), NULL, 10);
            v.pat.length = next_int(&p);
            v.pat.start_offset = next_int(&p);
            v.pat.n = (uint8_t)next_int(&p);
            for (int i = 0; i < v.pat.n; i++) {
                bl_pnote_t *q = &v.pat.note[i];
                q->position = next_int(&p);
                q->gate = next_int(&p);
                q->pitch.degree = (int32_t)next_int(&p);
                q->pitch.octave = (int32_t)next_int(&p);
                q->pitch.accidental = (int32_t)next_int(&p);
                q->velocity = (uint8_t)next_int(&p);
                q->accent = (uint8_t)next_int(&p);
                q->ratchet = (uint8_t)next_int(&p);
            }
            voices++;
        } else if (!strcmp(word, "window")) {
            window = next_int(&p);
            cursor = 0;
            snprintf(what, sizeof what, "%s (voice %d) window %lld", id, index, (long long)window);
        } else if (!strcmp(word, "w")) {
            bl_tick_t lo = next_int(&p), hi = lo + window < HARMONY_TOTAL ? lo + window : HARMONY_TOTAL;
            harmony_empty(&v, index, &sched, &key, &cursor, window, lo, what);
            int want = (int)next_int(&p), got = bl_render_voice(&v, index, &sched, &key, lo, hi, out, 256);
            CHECK(got == want, "harmony.ref:%d %s: window %lld: %d notes, want %d", line, what, (long long)lo, got,
                  want);
            for (int i = 0; i < want && i < got && i < 256; i++) {
                const bl_note_t *g = &out[i];
                int64_t w[7];
                for (int k = 0; k < 7; k++) w[k] = next_int(&p);
                /* LiveEngine.renderVoice's velocity: at the note's position in the loop */
                uint8_t vel = bl_velocity(g->velocity, g->accent, v.pat.seed,
                                          bl_floor_mod(g->at + v.pat.start_offset, v.pat.length));
                int same = g->voice == index && g->at == w[0] && g->midi == w[1] && g->velocity == w[2] &&
                           vel == w[3] && g->gate == w[4] && g->accent == w[5] && g->ratchet == w[6];
                CHECK(same, "harmony.ref:%d %s: window %lld note %d: %d %lld %d %d %d %lld %d %d, want %lld %lld %lld "
                      "%lld %lld %lld %lld", line, what, (long long)lo, i, g->voice, (long long)g->at, g->midi,
                      g->velocity, vel, (long long)g->gate, g->accent, g->ratchet, (long long)w[0], (long long)w[1],
                      (long long)w[2], (long long)w[3], (long long)w[4], (long long)w[5], (long long)w[6]);
                if (!same) break;
                harmony_notes++;
            }
            /* the buffer contract: a short buffer gets the first notes and the whole count, and nothing past it */
            if (want >= 2) {
                one[1].at = -1;
                got = bl_render_voice(&v, index, &sched, &key, lo, hi, one, 1);
                CHECK(got == want && one[0].at == out[0].at && one[0].midi == out[0].midi && one[1].at == -1,
                      "harmony.ref:%d %s: window %lld into one note: %d", line, what, (long long)lo, got);
            }
            harmony_windows++;
            cursor = lo + window;
        } else {
            CHECK(0, "harmony.ref:%d: unknown line %s", line, word);
        }
    }
    harmony_empty(&v, index, &sched, &key, &cursor, window, HARMONY_TOTAL, what);
    CHECK(scheds == 100 && bars == 50 && vels == 300 && voices == 19 && harmony_notes > 0 && clamped > 0,
          "harmony.ref: %d schedules, %d bars, %d velocities (%d clamped), %d voices, %d notes", scheds, bars, vels,
          clamped, voices, harmony_notes);
    printf("harmony: %d schedules, %d bars, %d velocities (%d clamped), %d voices, %d notes in %d windows\n", scheds,
           bars, vels, clamped, voices, harmony_notes, harmony_windows);
    free(text);
}

/* the schedule's bound: 2,000 merges of one event at increasing bars (every 7th again at the bar before, which it
 * replaces), now_bar 5 behind, against Swift's unbounded schedule kept here in an array, for bars 0-15 and now_bar
 * ... now_bar + 64; then a schedule full of events still to come, which refuses what would not fit and takes what
 * replaces */
static int32_t ref_offset(const bl_tev_t *ev, int n, int32_t bar)
{
    int32_t result = 0;
    for (int i = 0; i < n && ev[i].bar <= bar; i++) result = ev[i].offset;
    return result;
}

/* the unbounded schedule's merge of one event: replace its bar or insert in bar order */
static void ref_merge(bl_tev_t *ev, int *n, bl_tev_t e)
{
    int k = 0;
    while (k < *n && ev[k].bar < e.bar) k++;
    if (k == *n || ev[k].bar != e.bar) {
        memmove(ev + k + 1, ev + k, sizeof ev[0] * (size_t)(*n - k));
        (*n)++;
    }
    ev[k] = e;
}

/* a schedule of these events (sorted, at most BL_MAX_EVENTS), set directly */
static void fill_sched(bl_sched_t *s, const bl_tev_t *ev, int n)
{
    s->n = (uint16_t)n;
    memcpy(s->ev, ev, sizeof ev[0] * (size_t)n);
}

/* the answers compaction keeps: bars 0-15 and now_bar...upto, against the unbounded schedule */
static void check_domain(const bl_sched_t *s, const bl_tev_t *ref, int nref, int32_t now_bar, int32_t upto,
                         const char *what)
{
    CHECK(s->n <= BL_MAX_EVENTS, "%s: %d events", what, s->n);
    for (int32_t q = 0; q <= upto; q = q == 15 && now_bar > 16 ? now_bar : q + 1) {
        int32_t got = bl_sched_offset(s, q), want = ref_offset(ref, nref, q);
        CHECK(got == want, "%s: offset at bar %d %d, want %d", what, q, got, want);
    }
}

static void test_sched_compaction(void)
{
    static bl_tev_t ref[2000];
    static bl_sched_t s, before;
    int nref = 0, bad = 0, most = 0;
    int32_t bar = 0;
    s.n = 0;
    sm_state = 0x5C4ED;
    for (int i = 0; i < 2000 && bad < 5; i++) {
        if (i % 7 != 6) bar = i + i / 3;
        int32_t now_bar = bar < 5 ? 0 : bar - 5;
        bl_tev_t e = {bar, (int32_t)(sm_next() % 15) - 7};
        if (nref > 0 && ref[nref - 1].bar == bar)
            ref[nref - 1] = e;
        else
            ref[nref++] = e;
        int r = bl_sched_merge(&s, &e, 1, now_bar);
        CHECK(r == 0 && s.n <= BL_MAX_EVENTS, "merge %d (bar %d, now %d): %d, %d events", i, bar, now_bar, r, s.n);
        for (int k = 1; k < s.n; k++) CHECK(s.ev[k - 1].bar < s.ev[k].bar, "merge %d: not sorted at %d", i, k);
        for (int32_t q = 0; q <= now_bar + 64; q = q == 15 && now_bar > 16 ? now_bar : q + 1) {
            int32_t got = bl_sched_offset(&s, q), want = ref_offset(ref, nref, q);
            if (got != want) bad++;
            CHECK(got == want, "merge %d (bar %d, now %d): offset at bar %d %d, want %d", i, bar, now_bar, q, got,
                  want);
        }
        if (s.n > most) most = s.n;
    }
    CHECK(nref > BL_MAX_EVENTS && most == BL_MAX_EVENTS, "never compacted: %d events, at most %d kept", nref, most);

    /* full of events after now_bar: nothing to drop */
    s.n = 0;
    for (int32_t b = 1; b <= BL_MAX_EVENTS; b++) {
        bl_tev_t e = {b, b % 7};
        CHECK(bl_sched_merge(&s, &e, 1, 0) == 0, "fill: bar %d refused", b);
    }
    before = s;
    bl_tev_t two[2] = {{0, 3}, {BL_MAX_EVENTS + 1, 4}};
    CHECK(bl_sched_merge(&s, two, 2, 0) == -1 && !memcmp(&s, &before, sizeof s), "full: two new events taken");
    CHECK(bl_sched_merge(&s, two + 1, 1, 0) == -1 && !memcmp(&s, &before, sizeof s), "full: a new event taken");
    two[0].bar = 7;
    two[1].bar = 7;
    CHECK(bl_sched_merge(&s, two, 2, 0) == 0 && s.n == BL_MAX_EVENTS && bl_sched_offset(&s, 7) == 4 &&
              bl_sched_offset(&s, 8) == 1,
          "full: a replacement refused");

    /* a replaced bar that the compaction drops is new again (the review's case): bars 0-14, 20, 30, 31-269, now 30,
     * merging 20 and 1000 drops 20 and then has no room for two; merging 20 alone fits */
    static bl_tev_t full[BL_MAX_EVENTS + 8];
    int nfull = 0;
    for (int32_t b = 0; b < 270; b++)
        if (b < 15 || b == 20 || b >= 30) full[nfull++] = (bl_tev_t){b, b % 7 - 3};
    bl_tev_t add[4] = {{20, 5}, {1000, 6}};
    for (int k = 2; k >= 1; k--) {
        fill_sched(&s, full, nfull);
        int r = bl_sched_merge(&s, add, k, 30);
        nref = nfull;
        memcpy(ref, full, sizeof full[0] * (size_t)nfull);
        if (r == 0)
            for (int i = 0; i < k; i++) ref_merge(ref, &nref, add[i]);
        CHECK(r == (k == 2 ? -1 : 0) && s.n == (k == 2 ? BL_MAX_EVENTS - 1 : BL_MAX_EVENTS),
              "compacted, %d new: %d, %d events", k, r, s.n);
        check_domain(&s, ref, nref, 30, 1100, k == 2 ? "compacted, refused" : "compacted, replaced");
    }

    /* inserts out of bar order after a compaction: bars 0-14 and 16-256, now 200 (16-199 go), one replace */
    nfull = 0;
    for (int32_t b = 0; b <= 256; b++)
        if (b != 15) full[nfull++] = (bl_tev_t){b, b % 5 - 2};
    fill_sched(&s, full, nfull);
    nref = nfull;
    memcpy(ref, full, sizeof full[0] * (size_t)nfull);
    bl_tev_t odd[4] = {{400, 1}, {15, 6}, {300, 2}, {250, -4}};
    CHECK(bl_sched_merge(&s, odd, 4, 200) == 0 && s.n == 15 + 57 + 3, "out of order: %d events", s.n);
    for (int i = 0; i < 4; i++) ref_merge(ref, &nref, odd[i]);
    for (int k = 1; k < s.n; k++) CHECK(s.ev[k - 1].bar < s.ev[k].bar, "out of order: not sorted at %d", k);
    check_domain(&s, ref, nref, 200, 500, "out of order");
}

/* ---------------------------------------------------------- arrangement --- */
/* tests/equiv/arrange.ref: each case run again by arrange.c, bar by bar: the state after every tickBar and advance
 * (in the q and z runs too, there the whole SM against the bar before) and the directives against the file's */
static const char *const DIR_NAME[] = {"activate", "deactivate", "nudge", "accent", "blank", "density", "bpart", "thin",
                                       "stop"};

typedef struct {
    char name[16];
    int n;
    char id[BL_MAX_VOICES][16];
    uint8_t role[BL_MAX_VOICES];
} arr_list_t;

static int arr_line;
static const char *arr_what;

static int arr_voice(const arr_list_t *list, const char *id)
{
    for (int i = 0; i < list->n; i++)
        if (!strcmp(list->id[i], id)) return i;
    return -1;
}

/* a comma-joined set of ids, `{}` empty, as a mask */
static uint32_t arr_mask(const arr_list_t *list, char *w)
{
    uint32_t m = 0;
    if (!strcmp(w, "{}")) return 0;
    for (char *id = strtok(w, ","); id; id = strtok(NULL, ",")) {
        int v = arr_voice(list, id);
        CHECK(v >= 0, "arrange.ref:%d: unknown voice %s", arr_line, id);
        if (v >= 0) m |= 1u << v;
    }
    return m;
}

/* the SM's state as the file writes it: phase, barsInPhase, barsUntilChange, activeVoices */
static void arr_state(const arr_list_t *list, const bl_arr_t *a, char *buf, size_t size)
{
    int k = snprintf(buf, size, "%s %d %d ", BL_PHASE_RAW[a->phase], a->bars_in_phase, a->bars_until_change);
    if (!a->nactive) snprintf(buf + k, size - (size_t)k, "-");
    for (int i = 0; i < a->nactive; i++)
        k += snprintf(buf + k, size - (size_t)k, "%s%s", i ? "," : "", list->id[a->active[i]]);
}

/* every field of the SM (a struct copy need not copy the padding, so no memcmp) */
static int arr_same(const bl_arr_t *x, const bl_arr_t *y)
{
    int same = x->cfg.enabled == y->cfg.enabled && x->cfg.ambient == y->cfg.ambient &&
               x->cfg.reprise == y->cfg.reprise && x->cfg.min_bars == y->cfg.min_bars &&
               x->cfg.max_bars == y->cfg.max_bars && x->phase == y->phase && x->bars_in_phase == y->bars_in_phase &&
               x->nactive == y->nactive && x->norder == y->norder && x->ambient_period == y->ambient_period &&
               x->density.bits == y->density.bits && x->rng.state == y->rng.state &&
               x->bars_until_change == y->bars_until_change && x->events_in_phase == y->events_in_phase &&
               x->round_robin == y->round_robin && x->stop_requested == y->stop_requested;
    for (int i = 0; same && i < x->nactive; i++) same = x->active[i] == y->active[i];
    for (int i = 0; same && i < x->norder; i++) same = x->order[i] == y->order[i];
    return same;
}

/* one tickBar (advance 0) or advance, against the line's state and directives */
static void arr_step(const arr_list_t *list, bl_arr_t *a, uint32_t locked, int advance, char **p)
{
    bl_dir_t out[BL_ARR_MAX_DIRS], one[2];
    bl_arr_t copy = *a;
    int n = advance ? bl_arr_advance(a, locked, out, BL_ARR_MAX_DIRS) : bl_arr_tick_bar(a, locked, out, BL_ARR_MAX_DIRS);
    char got[160], want[160];
    arr_state(list, a, got, sizeof got);
    const char *ph = next_word(p), *bip = next_word(p), *buc = next_word(p), *act = next_word(p);
    snprintf(want, sizeof want, "%s %s %s %s", ph, bip, buc, act);
    CHECK(!strcmp(got, want), "arrange.ref:%d %s: state %s, want %s", arr_line, arr_what, got, want);
    int i = 0;
    for (char *w = next_word(p); *w; w = next_word(p), i++) {
        char *arg = strchr(w, ':');
        if (arg) *arg++ = 0;
        int kind = name_index(DIR_NAME, 9, w);
        int same = i < n && out[i].kind == kind;
        if (same && kind == BL_DIR_DENSITY) same = out[i].value.bits == strtoull(arg, NULL, 16);
        else if (same && arg) same = !strcmp(list->id[out[i].voice], arg);
        CHECK(same, "arrange.ref:%d %s: directive %d: %d %d %016llx, want %s %s", arr_line, arr_what, i,
              i < n ? out[i].kind : -1, i < n ? out[i].voice : -1,
              i < n ? (unsigned long long)out[i].value.bits : 0ull, w, arg ? arg : "");
    }
    CHECK(n == i, "arrange.ref:%d %s: %d directives, want %d", arr_line, arr_what, n, i);
    /* the buffer contract: a short buffer gets the first directive and the whole count, nothing past it, and the
     * same state */
    if (n >= 2) {
        one[1].kind = 0xFF;
        int m = advance ? bl_arr_advance(&copy, locked, one, 1) : bl_arr_tick_bar(&copy, locked, one, 1);
        CHECK(m == n && one[0].kind == out[0].kind && one[0].voice == out[0].voice && one[1].kind == 0xFF &&
                  arr_same(&copy, a),
              "arrange.ref:%d %s: into one directive: %d", arr_line, arr_what, m);
    }
}

static void test_arrange_vectors(void)
{
    char *text = read_file("tests/equiv/arrange.ref");
    CHECK(text != NULL, "tests/equiv/arrange.ref: cannot read");
    if (!text) return;
    static arr_list_t lists[4];
    static bl_arrcfg_t presets[4];
    static char preset_name[4][16];
    static bl_arr_t a;
    const arr_list_t *list = &lists[0];
    uint32_t locked = 0;
    int nlists = 0, npresets = 0, cases = 0, steps = 0, advances = 0, bar = 400, stops = 0, relocks = 0;
    char *save = text, what[160] = "";
    arr_what = what;
    arr_line = 0;
    for (char *l = text; l; l = save) {
        char *nl = strchr(l, '\n');
        save = nl ? nl + 1 : NULL;
        if (nl) *nl = 0;
        arr_line++;
        if (!*l || *l == '#') continue;
        char *p = l, *word = next_word(&p);
        if (!strcmp(word, "preset")) {
            bl_arrcfg_t *c = &presets[npresets];
            snprintf(preset_name[npresets++], 16, "%s", next_word(&p));
            c->enabled = (uint8_t)next_int(&p);
            c->ambient = (uint8_t)next_int(&p);
            c->reprise = (uint8_t)next_int(&p);
            c->min_bars = (int16_t)next_int(&p);
            c->max_bars = (int16_t)next_int(&p);
        } else if (!strcmp(word, "voices")) {
            arr_list_t *v = &lists[nlists++];
            snprintf(v->name, sizeof v->name, "%s", next_word(&p));
            for (v->n = 0; *p; v->n++) {
                snprintf(v->id[v->n], sizeof v->id[0], "%s", next_word(&p));
                v->role[v->n] = (uint8_t)name_index(ROLE_NAME, 8, next_word(&p));
            }
        } else if (!strcmp(word, "order")) {
            next_word(&p);
            const arr_list_t *v = &lists[nlists - 1];
            uint8_t order[BL_MAX_VOICES];
            int n = bl_build_order(v->role, v->n, order), i = 0;
            for (char *w = next_word(&p); *w; w = next_word(&p), i++)
                CHECK(i < n && !strcmp(v->id[order[i]], w), "arrange.ref:%d: build order of %s: %d is %s, want %s",
                      arr_line, v->name, i, i < n ? v->id[order[i]] : "-", w);
            CHECK(n == i && n == v->n, "arrange.ref:%d: build order of %s: %d voices, want %d", arr_line, v->name, n,
                  i);
        } else if (!strcmp(word, "case")) {
            CHECK(bar == 400, "arrange.ref:%d %s: %d bars, want 400", arr_line, what, bar);
            const char *ln = next_word(&p), *pn = next_word(&p);
            for (int i = 0; i < nlists; i++)
                if (!strcmp(lists[i].name, ln)) list = &lists[i];
            int preset = -1;
            for (int i = 0; i < npresets; i++)
                if (!strcmp(preset_name[i], pn)) preset = i;
            uint64_t master = strtoull(next_word(&p), NULL, 10), seed = strtoull(next_word(&p), NULL, 10);
            int period = (int)next_int(&p);
            char *adopt = next_word(&p);
            int adopting = strcmp(adopt, "-") != 0;
            snprintf(what, sizeof what, "%s %s %llu period %d adopt %s", ln, pn, (unsigned long long)master, period,
                     adopt);
            uint32_t active = adopting ? arr_mask(list, adopt) : 0;
            locked = arr_mask(list, next_word(&p));
            CHECK(!strcmp(list->name, ln) && preset >= 0, "arrange.ref:%d: unknown list or preset", arr_line);
            CHECK(bl_seed_derive(master, "arrangement", (const char *)NULL) == seed,
                  "arrange.ref:%d %s: the seed", arr_line, what);
            bl_arr_init(&a, &presets[preset], list->role, list->n, seed, adopting, active, period);
            bar = 0;
            cases++;
        } else if (!strcmp(word, "init")) {
            char got[160], want[160];
            arr_state(list, &a, got, sizeof got);
            snprintf(want, sizeof want, "%s", p);
            CHECK(!strcmp(got, want), "arrange.ref:%d %s: init %s, want %s", arr_line, what, got, want);
        } else if (!strcmp(word, "lock")) {
            int at = (int)next_int(&p);
            CHECK(at == bar + 1, "arrange.ref:%d %s: lock at bar %d, want %d", arr_line, what, bar + 1, at);
            locked = arr_mask(list, next_word(&p));
            relocks++;
        } else if (!strcmp(word, "q") || !strcmp(word, "z")) {
            /* k bars without directives: the clock alone moves (q), or nothing does (z) */
            int k = (int)next_int(&p), d = *word == 'q';
            for (int i = 0; i < k; i++, bar++, steps++) {
                bl_dir_t out[BL_ARR_MAX_DIRS];
                bl_arr_t before = a;
                int n = bl_arr_tick_bar(&a, locked, out, BL_ARR_MAX_DIRS);
                before.bars_in_phase += d;
                before.bars_until_change -= d;
                CHECK(n == 0 && arr_same(&before, &a), "arrange.ref:%d %s: bar %d: %d directives or not %s",
                      arr_line, what, bar + 1, n, d ? "the clock alone" : "still");
            }
        } else if (!strcmp(word, "t") || !strcmp(word, "a")) {
            int at = (int)next_int(&p), adv = *word == 'a';
            if (!adv) bar++;
            CHECK(at == bar + adv, "arrange.ref:%d %s: at bar %d, want %d", arr_line, what, bar + adv, at);
            if (strstr(p, " stop")) stops++;
            arr_step(list, &a, locked, adv, &p);
            if (adv) advances++;
            else steps++;
        } else {
            CHECK(0, "arrange.ref:%d: unknown line %s", arr_line, word);
        }
    }
    CHECK(bar == 400, "arrange.ref:%d %s: %d bars, want 400", arr_line, what, bar);
    CHECK(npresets == 4 && nlists == 4 && cases == 1218 && steps == 1218 * 400 && advances == 1218 * 10 &&
              relocks == 180 && stops > 1000,
          "arrange.ref: %d presets, %d voice lists, %d cases, %d bars, %d advances, %d lock changes, %d stops",
          npresets, nlists, cases, steps, advances, relocks, stops);
    /* the file's presets are classicArc, slowBuild, ambientPlateau, then the default */
    const bl_arrcfg_t *const ours[] = {&bl_arr_classic, &bl_arr_slow, &bl_arr_ambient};
    for (int i = 0; i < npresets && i < 3; i++)
        CHECK(ours[i]->enabled == presets[i].enabled && ours[i]->ambient == presets[i].ambient &&
                  ours[i]->reprise == presets[i].reprise && ours[i]->min_bars == presets[i].min_bars &&
                  ours[i]->max_bars == presets[i].max_bars,
              "arrange.ref: the preset %s", preset_name[i]);
    printf("arrange: %d cases, %d bars, %d advances, %d lock changes, %d stops\n", cases, steps, advances, relocks,
           stops);
    free(text);
}

int main(void)
{
    test_f64_ops();
    test_sin_cc_level();
    test_sin_range();
    test_rng_vectors();
    test_music();
    test_gen_vectors();
    test_gen_max_notes();
    test_mut_vectors();
    test_legit_lifecycle();
    test_harmony_vectors();
    test_sched_compaction();
    test_arrange_vectors();
    printf(fails ? "engine: %d FAILED\n" : "engine: ok\n", fails);
    return fails != 0;
}
