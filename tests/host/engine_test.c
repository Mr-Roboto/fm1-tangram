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
    int lines = 0, values = 0;
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
                if (!strcmp(word, "in")) got = bl_rand_in(&r, lo, hi);
                else if (!strcmp(word, "upto")) got = bl_rand_upto(&r, lo, hi);
                else if (!strcmp(word, "bool")) got = bl_rand_bool(&r);
                else if (!strcmp(word, "index")) got = bl_rand_index(&r, count);
                else {
                    CHECK(0, "rng.ref: unknown line %s", word);
                    break;
                }
                CHECK(got == want, "%s %llu #%d: %lld, want %lld", word, (unsigned long long)seed, i, (long long)got,
                      (long long)want);
            }
            values++;
            p = end;
        }
    }
    CHECK(lines >= 100 && values > 30000, "rng.ref: only %d lines, %d values", lines, values);
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

int main(void)
{
    test_f64_ops();
    test_sin_cc_level();
    test_sin_range();
    test_rng_vectors();
    printf(fails ? "engine: %d FAILED\n" : "engine: ok\n", fails);
    return fails != 0;
}
