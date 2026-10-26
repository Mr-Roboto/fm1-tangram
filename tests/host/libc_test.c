/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The firmware's own libc (app/libc/libc.c) on the host, under other names (the host's libc keeps its own): the
 * 64-bit divide against the host's over a million pairs of every magnitude and sign, memmove both ways over an
 * overlap, strcmp's sign. */
#include <stdint.h>
#include <stdio.h>
#include "test.h"

#define memmove t_memmove
#define strcmp t_strcmp
#define abs t_abs
#define __udivdi3 t_udivdi3
#define __umoddi3 t_umoddi3
#define __divdi3 t_divdi3
#define __moddi3 t_moddi3
#include "../../app/libc/libc.c"

static uint64_t rnd_state = UINT64_C(0x9E3779B97F4A7C15);
static uint64_t rnd(void)                /* xorshift64 */
{
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 7;
    rnd_state ^= rnd_state << 17;
    return rnd_state;
}

static uint64_t any(void)                /* a value of 0 to 64 bits */
{
    int bits = (int)(rnd() % 65);
    return bits ? rnd() >> (64 - bits) : 0;
}

static void test_div64(void)
{
    static const int64_t EDGE[] = {0, 1, -1, 2, -2, 10, -10, 3840, -3840, INT32_MAX, INT32_MIN, (int64_t)UINT32_MAX,
                                   -(int64_t)UINT32_MAX, INT64_MAX, INT64_MIN, INT64_MAX - 1, INT64_MIN + 1};
    int n = (int)(sizeof EDGE / sizeof EDGE[0]), i;
    for (i = 0; i < 1000000 + n * n; i++) {
        uint64_t a, b;
        if (i < n * n) {
            a = (uint64_t)EDGE[i / n];
            b = (uint64_t)EDGE[i % n];
        } else {
            a = any();
            b = any();
            if (rnd() & 1) a = 0 - a;
            if (rnd() & 1) b = 0 - b;
        }
        if (!b) continue;
        CHECK(t_udivdi3(a, b) == a / b && t_umoddi3(a, b) == a % b, "%llx u/%% %llx", (unsigned long long)a,
              (unsigned long long)b);
        if ((int64_t)a != INT64_MIN || (int64_t)b != -1) {     /* (C's one overflow) */
            int64_t sa = (int64_t)a, sb = (int64_t)b;
            CHECK(t_divdi3(sa, sb) == sa / sb && t_moddi3(sa, sb) == sa % sb, "%lld /%% %lld", (long long)sa,
                  (long long)sb);
        }
        if (fails > 20) return;
    }
}

static void test_mem_str(void)
{
    char s[16] = "abcdefghij";
    t_memmove(s + 2, s, 6);              /* over an overlap, to above the source */
    CHECK(!t_strcmp(s, "ababcdefij"), "memmove up: %s", s);
    t_memmove(s, s + 3, 6);              /* and to below it */
    CHECK(!t_strcmp(s, "bcdefiefij"), "memmove down: %s", s);
    CHECK(t_strcmp("a", "b") < 0 && t_strcmp("b", "a") > 0 && t_strcmp("ab", "abc") < 0, "strcmp order");
    CHECK(t_strcmp("\xff", "a") > 0, "strcmp: bytes are unsigned");
    CHECK(t_abs(-7) == 7 && t_abs(7) == 7 && t_abs(0) == 0, "abs");
}

int main(void)
{
    test_div64();
    test_mem_str();
    printf(fails ? "libc: %d FAILED\n" : "libc: ok\n", fails);
    return fails != 0;
}
