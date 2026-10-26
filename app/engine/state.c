/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* A state as bytes and back (state.h). Each value is checked against its field's range on the way in and against
 * what an engine state may hold on the way out (a slot read from the flash is recalled into the running engine). */
#include <string.h>
#include "state.h"
#include "gen.h"

/* ------------------------------------------------------------- writing --- */
typedef struct {
    uint8_t *b;
    int n, max, bad;
} out_t;

static void put8(out_t *o, uint32_t v)
{
    if (o->n >= o->max) {
        o->bad = 1;
        return;
    }
    o->b[o->n++] = (uint8_t)v;
}

static void put16(out_t *o, uint32_t v)
{
    put8(o, v & 0xFF);
    put8(o, v >> 8);
}

static void put32(out_t *o, uint32_t v)
{
    put16(o, v & 0xFFFF);
    put16(o, v >> 16);
}

static void put64(out_t *o, uint64_t v)
{
    put32(o, (uint32_t)v);
    put32(o, (uint32_t)(v >> 32));
}

static void put_leb(out_t *o, uint32_t v)        /* 7 bits a byte, the low ones first, the high bit "more" */
{
    for (; v >= 0x80; v >>= 7) put8(o, (v & 0x7F) | 0x80);
    put8(o, v);
}

/* the value if lo <= v <= hi, else the out's bad */
static uint32_t in_range(out_t *o, int64_t v, int64_t lo, int64_t hi)
{
    if (v < lo || v > hi) o->bad = 1;
    return (uint32_t)v;
}

int state_pack(const bl_state_t *s, uint8_t *buf, int max)
{
    out_t o = {buf, 0, max, 0};
    put8(&o, in_range(&o, s->n, 0, BL_MAX_VOICES));
    for (int i = 0; i < s->n && i < BL_MAX_VOICES; i++) {
        const bl_voice_t *v = &s->v[i];
        const bl_pattern_t *p = &v->pat;
        put8(&o, in_range(&o, v->id ? bl_vid(v->id) : BL_VID_NONE, 0, BL_NVIDS - 1));
        put8(&o, v->role);
        put8(&o, v->reg_low);
        put8(&o, v->reg_high);
        put8(&o, in_range(&o, v->ref_c, 0, 127));
        put8(&o, (v->participates ? 1u : 0u) | (p->locked ? 2u : 0u) | in_range(&o, v->anchor, 0, 3) << 2);
        put8(&o, p->generator);
        put8(&o, in_range(&o, p->remaining, -1, 127) & 0xFF);
        put8(&o, in_range(&o, p->n, 0, BL_MAX_NOTES));
        put32(&o, in_range(&o, p->length, 1, UINT32_MAX));
        put32(&o, in_range(&o, p->start_offset, 0, p->length - 1));
        put32(&o, in_range(&o, s->generation[i], 0, UINT32_MAX));
        put64(&o, p->seed);
        for (int k = 0; k < p->n && k < BL_MAX_NOTES; k++) {
            const bl_pnote_t *q = &p->note[k];
            put16(&o, in_range(&o, q->position, 0, p->length - 1 < 0xFFFF ? p->length - 1 : 0xFFFF));
            put16(&o, in_range(&o, q->gate, 1, 0xFFFF));
            put8(&o, in_range(&o, q->pitch.degree, -128, 127) & 0xFF);
            put8(&o, in_range(&o, q->pitch.octave, -128, 127) & 0xFF);
            put8(&o, in_range(&o, q->pitch.accidental, -128, 127) & 0xFF);
            put8(&o, q->velocity);
            put8(&o, (q->accent ? 1u : 0u) | in_range(&o, q->ratchet, 0, 127) << 1);
        }
    }
    put16(&o, in_range(&o, s->sched.n, 0, BL_MAX_EVENTS));
    int32_t bar = 0;
    for (int k = 0; k < s->sched.n && k < BL_MAX_EVENTS; k++) {
        const bl_tev_t *e = &s->sched.ev[k];
        put_leb(&o, in_range(&o, (int64_t)e->bar - bar, k ? 1 : 0, INT32_MAX));   /* sorted, one event a bar */
        put8(&o, in_range(&o, e->offset, -128, 127) & 0xFF);
        bar = e->bar;
    }
    return o.bad ? -1 : o.n;
}

/* ------------------------------------------------------------- reading --- */
typedef struct {
    const uint8_t *b;
    int n, at, bad;
} in_t;

static uint32_t get8(in_t *in)
{
    if (in->at >= in->n) {
        in->bad = 1;
        return 0;
    }
    return in->b[in->at++];
}

static uint32_t get16(in_t *in)
{
    uint32_t lo = get8(in);
    return lo | get8(in) << 8;
}

static uint32_t get32(in_t *in)
{
    uint32_t lo = get16(in);
    return lo | get16(in) << 16;
}

static uint64_t get64(in_t *in)
{
    uint64_t lo = get32(in);
    return lo | (uint64_t)get32(in) << 32;
}

static uint32_t get_leb(in_t *in)               /* at most 5 bytes: a u32 */
{
    uint32_t v = 0, b;
    int shift = 0;
    do {
        b = get8(in);
        if (shift == 28 && b > 0x0F) in->bad = 1;
        v |= (b & 0x7F) << shift;
        shift += 7;
    } while ((b & 0x80) && shift < 35 && !in->bad);
    if (b & 0x80) in->bad = 1;
    return v;
}

static int32_t sign8(uint32_t b) { return b < 0x80 ? (int32_t)b : (int32_t)b - 256; }

/* the bad of in, when v is outside lo...hi */
static void check(in_t *in, int64_t v, int64_t lo, int64_t hi)
{
    if (v < lo || v > hi) in->bad = 1;
}

int state_unpack(const uint8_t *buf, int n, bl_state_t *out)
{
    in_t in = {buf, n, 0, 0};
    memset(out, 0, sizeof *out);
    out->n = (uint8_t)get8(&in);
    check(&in, out->n, 0, BL_MAX_VOICES);
    for (int i = 0; i < out->n && !in.bad; i++) {
        bl_voice_t *v = &out->v[i];
        bl_pattern_t *p = &v->pat;
        uint32_t vid = get8(&in);
        check(&in, vid, 0, BL_NVIDS - 1);
        v->id = BL_VID_NAME[vid < BL_NVIDS ? vid : 0];
        v->role = (uint8_t)get8(&in);
        check(&in, v->role, BL_ROLE_BED1, BL_ROLE_AUX);
        v->reg_low = (uint8_t)get8(&in);
        v->reg_high = (uint8_t)get8(&in);
        check(&in, v->reg_low, 0, v->reg_high);
        check(&in, v->reg_high, 0, 127);
        v->ref_c = (int32_t)get8(&in);
        check(&in, v->ref_c, 0, 127);
        uint32_t flags = get8(&in);
        v->participates = flags & 1u;
        p->locked = (flags >> 1) & 1u;
        v->anchor = (uint8_t)(flags >> 2);
        check(&in, v->anchor, BL_ANCHOR_NONE, BL_ANCHOR_BEATS13);
        p->generator = (uint8_t)get8(&in);
        p->remaining = (int16_t)sign8(get8(&in));
        check(&in, p->remaining, -1, 127);
        p->n = (uint8_t)get8(&in);
        check(&in, p->n, 0, BL_MAX_NOTES);
        p->length = get32(&in);
        check(&in, p->length, 1, UINT32_MAX);
        p->start_offset = get32(&in);
        check(&in, p->start_offset, 0, p->length - 1);
        out->generation[i] = get32(&in);
        p->seed = get64(&in);
        for (int k = 0; k < p->n && !in.bad; k++) {
            bl_pnote_t *q = &p->note[k];
            q->position = get16(&in);
            check(&in, q->position, 0, p->length - 1);
            q->gate = get16(&in);
            check(&in, q->gate, 1, 0xFFFF);
            q->pitch.degree = sign8(get8(&in));
            q->pitch.octave = sign8(get8(&in));
            q->pitch.accidental = sign8(get8(&in));
            q->velocity = (uint8_t)get8(&in);
            uint32_t nf = get8(&in);
            q->accent = nf & 1u;
            q->ratchet = (uint8_t)(nf >> 1);
        }
    }
    uint32_t nev = in.bad ? 0 : get16(&in);
    check(&in, nev, 0, BL_MAX_EVENTS);
    int64_t bar = 0;
    for (uint32_t k = 0; k < nev && !in.bad; k++) {
        uint32_t step = get_leb(&in);
        check(&in, step, k ? 1 : 0, INT32_MAX);
        bar += step;
        check(&in, bar, 0, INT32_MAX);
        out->sched.ev[k].bar = (int32_t)bar;
        out->sched.ev[k].offset = sign8(get8(&in));
    }
    out->sched.n = (uint16_t)nev;
    return in.bad || in.at != n ? -1 : 0;
}
