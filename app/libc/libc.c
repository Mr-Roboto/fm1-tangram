/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The libc the engine's units call on the FM-1 beyond felucca-bsp's (chip/libc.c: memset, memcpy, memcmp), declared by
 * this directory's string.h and stdlib.h: memmove, strcmp, abs, and the compiler's 64-bit divide (an int64_t / or %
 * by a variable: __divdi3 __moddi3 __udivdi3 __umoddi3; the toolchain's own runtime is linked only with the radio).
 * The firmware's unit (app.c) includes it; the host has its own libc (tests/host/libc_test.c checks this one there). */

void *memmove(void *d, const void *s, unsigned n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    if (p < q)
        while (n--)
            *p++ = *q++;
    else
        while (n--)
            p[n] = q[n];
    return d;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int abs(int v) { return v < 0 ? -v : v; }

/* n / d and n % d: shift and subtract, no multiply (felucca-bsp docs/CPU.md); both in 32 bits, the CPU's divide.
 * d 0 gives all ones and n (C leaves it undefined) */
static __attribute__((noinline)) uint64_t udivmod64(uint64_t n, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0, r = 0;
    int i;
    if (!((n | d) >> 32) && d) {
        *rem = (uint32_t)n % (uint32_t)d;
        return (uint32_t)n / (uint32_t)d;
    }
    if (!d || n < d) {
        *rem = n;
        return d ? 0 : ~(uint64_t)0;
    }
    for (i = 63; i >= 0; i--) {
        r = r << 1 | (n >> i & 1u);
        if (r >= d) {
            r -= d;
            q |= (uint64_t)1 << i;
        }
    }
    *rem = r;
    return q;
}

uint64_t __udivdi3(uint64_t n, uint64_t d)
{
    uint64_t r;
    return udivmod64(n, d, &r);
}

uint64_t __umoddi3(uint64_t n, uint64_t d)
{
    uint64_t r;
    udivmod64(n, d, &r);
    return r;
}

/* toward zero, the remainder with the dividend's sign (C's) */
int64_t __divdi3(int64_t n, int64_t d)
{
    uint64_t r, q = udivmod64(n < 0 ? 0 - (uint64_t)n : (uint64_t)n, d < 0 ? 0 - (uint64_t)d : (uint64_t)d, &r);
    return (int64_t)((n < 0) != (d < 0) ? 0 - q : q);
}

int64_t __moddi3(int64_t n, int64_t d)
{
    uint64_t r;
    udivmod64(n < 0 ? 0 - (uint64_t)n : (uint64_t)n, d < 0 ? 0 - (uint64_t)d : (uint64_t)d, &r);
    return (int64_t)(n < 0 ? 0 - r : r);
}
