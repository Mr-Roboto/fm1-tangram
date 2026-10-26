/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan's transport on the FM-1 (Transport.swift, with EventConversion.swift's MIDI, Ratchet.swift's retriggers
 * and NoteRegistry.swift's silencing): the live engine rendered ahead of the sounding tick, its notes, CC and clock
 * as timed MIDI in a queue, the queue sent out over USB from the 1 kHz tick, and the clock itself: INT (the FM-1 the
 * master: clock, Start and Stop out) or EXT (it follows F8 from USB, the TRS jack or both). Part of the unity build.
 *
 * Two contexts share it. The main loop (app_loop) renders and takes the controls (rt_play ... rt_set_ext_port); the
 * tick (app_tick, the timer interrupt) reads MIDI in, moves the clock and sends: first what is to go out at once
 * (Stop, a flush), then what is due, RT_SEND_MS packets a ms at most (the device's USB-MIDI out ring holds 64 and drops
 * silently; the rest waits for the next ms). What both touch is under plat_lock: the queues, the clock, the registry,
 * playing. The engine is the main loop's alone.
 *
 * As on the Mac:
 * - Play: a fresh engine of the project from tick 0, which sounds RT_PREROLL_MS after the press (INT; EXT: at the
 *   press or the FA, at the tempo in store until the pulses lead, after an FA short of its first pulse, tick 0);
 *   MIDI Start at tick 0 and a clock pulse every 40 ticks (24 PPQ) when INT.
 * - Windows [head, end) follow one another; the actions bind at the head (rt_action); a tempo change (INT) and a
 *   loaded project bind at the head's next bar line, the window ending there: the project's engine takes over with
 *   its tick 0 on that bar (origin), the mutes cleared, the channels its own, its tempo adopted (INT; a tempo
 *   change pending there still wins). What the old engine has queued (its notes' offs) still goes out.
 * - A window's MIDI: NoteEventConverter.events of its notes (muted voices dropped, ratchets expanded: slice 240 /
 *   ratchet, sub-gate max(1, min(slice * 9 / 10, gate)); the off at at + gate), ccEvents of its CC (a mute never
 *   drops CC), clockTicks of its range (INT), in that order into the queue, which keeps them by tick, a stable
 *   order: what goes out is the stable sort by tick of every window's list, as CoreMIDI sends the Mac's.
 * - Stop: MIDI Stop (INT), then the registry's flush: a note-off for every note sounding, by (channel, note), then
 *   CC 123 (All Notes Off) on each channel that had one. Mute flushes the voice's channel; a channel change the old
 *   one; the arranger's ending stops at its tick. The registry holds what was handed to plat_midi_send.
 * Where it is the FM-1's own:
 * - The windows are those of the reference's `window 240` (tests/equiv/README.md): to the next multiple of 240, the
 *   bar line of a pending change, rendered while the head is less than RT_LOOKAHEAD_MS ahead of the sounding tick
 *   (the Mac renders to now + 150 ms, its windows as long as its wakes). Same windows, same order on the wire.
 * - The queue is the FM-1's, so a flush also takes out what it holds of the voice on the flushed channel (mute,
 *   channel) or everything (stop) instead of sending the offs twice as the Mac does past CoreMIDI's.
 * - EXT: each pulse is 40 ticks. Between pulses the clock runs at the rate of the last beat's pulses (24 intervals),
 *   never past the next pulse; after RT_FREEWHEEL_MS without one it runs on at that rate (the router unplugged, the
 *   DAW paused), and the next pulse takes up from the tick reached, never back. FA / FB start, FC stops. The tempo
 *   (rt_bpm) is the followed one.
 * - The registry knows what went out on the wire (the tick sends it), not what was scheduled. */
#include "runtime.h"

#define RT_WINDOW 240                   /* the windows' grid, in ticks: the longest window */
#define RT_LOOKAHEAD_MS 100
#define RT_PREROLL_MS 100                /* Transport's prerollNanos */
#define RT_QUEUE 1024                    /* timed events (a power of two) */
#define RT_NOW 256                       /* events to send at once: Stop, a flush (a power of two) */
#define RT_SEND_MS 16                    /* packets sent a ms at most */
#define RT_NOTES 128                     /* a window's notes and CC (240 ticks of the templates: at most 9 and 8) */
#define RT_CC 128
#define RT_PULSE 40                      /* ticks per MIDI clock: 960 / 24 */
#define RT_STEP (BL_PPQN / 4)            /* Ratchet.stepTicks: a ratchet's hits share one 16th */
#define RT_FREEWHEEL_MS 500
#define RT_RING 25                       /* the pulse times the EXT rate is measured over: one beat */
#define RT_NONE 0xFFu                    /* the channel of a voice the running project lacks */

typedef struct {
    bl_tick_t tick;
    uint32_t pkt;                        /* a USB-MIDI packet (plat.h) */
    uint8_t vid;                         /* the voice's BL_VID_ (BL_VID_NONE: the clock, a flush) */
} rt_ev_t;

static bl_live_t rt_eng PLAT_POOL;       /* the running engine */
static bl_project_t rt_proj PLAT_POOL;   /* Transport.project: the next Play's, the running engine's */
static bl_project_t rt_next PLAT_POOL;   /* pendingProject: takes over at the head's next bar line */
static bl_note_t rt_note[RT_NOTES] PLAT_POOL;
static bl_cc_t rt_cc[RT_CC] PLAT_POOL;
static rt_ev_t rt_q[RT_QUEUE] PLAT_POOL;
static rt_ev_t rt_nq[RT_NOW] PLAT_POOL;  /* to send at once, before the queue */
#define RT_Q(i) rt_q[(rt.qr + (i)) & (RT_QUEUE - 1u)]

static struct {
    /* the main loop's */
    bl_tick_t head, origin;              /* the render head; where the running engine's tick 0 sits */
    uint8_t has_next, stop_queued, ext, clock_out;
    uint8_t fresh;                       /* the engine is not rt_proj's yet (none since it came stopped): its preview */
    uint8_t muted;                       /* mutedVoices: a bit per BL_VID_ */
    uint8_t chan[BL_NVIDS];              /* channelMap by BL_VID_, RT_NONE: none */
    uint8_t vid[BL_MAX_VOICES];          /* the running engine's voices' BL_VID_ */
    int32_t bpm, pend_bpm;               /* liveBPM (EXT: the tick's, the followed tempo); pendingBPM, -1 none */
    uint32_t dropped, over;              /* events the full queue refused; notes or CC past a window's buffer */
    /* shared with the tick, under plat_lock */
    volatile uint8_t playing, start_req; /* start_req: an FA / FB the main loop is to play */
    uint8_t started, ext_run, pulsed, fa, ext_ports, npm;   /* fa: started by an FA, its first pulse to come */
    uint16_t pre;                        /* INT: ms to tick 0 */
    uint16_t qr, qn, nr, nn;             /* the queue, the at-once one: its first event, how many */
    bl_tick_t tick;                      /* the sounding tick (started), and its thousandths */
    int32_t frac, rate;                  /* rate: thousandths of a tick per ms (INT: bpm * 16) */
    bl_tick_t swap_tick, tempo_tick;     /* a tempo's clock segment from swap_tick (-1 none); where the last began */
    bl_tick_t stop_at;                   /* the arranger's ending: the transport stops there (-1 none) */
    int32_t swap_rate;
    bl_tick_t pulse_tick;                /* EXT: the last pulse's tick */
    uint32_t last_pulse, fa_ms, pulse_ms[RT_RING];   /* its ms; the FA's; the latest pulses' ms (npm of them) */
    uint32_t snd[16][4];                 /* NoteRegistry: a bit per (channel, note) sounding */
} rt;

#ifdef PLAT_HOST
static void host_sent(bl_tick_t tick, uint32_t pkt);   /* host.c: dumpmidi */
static bl_tick_t (*rt_host_window)(bl_tick_t head);   /* host.c: a test's hook at each window's start (its actions
                                                         * and mutes); a window ends at its answer at the latest */
#endif

static uint32_t rt_pkt(uint32_t st, uint32_t d1, uint32_t d2)
{
    return (st >= 0xF0u ? 0x0Fu : st >> 4) | st << 8 | d1 << 16 | d2 << 24;
}

/* Transport.nextBar: the first bar line at or after t, bars counted from the running engine's origin (4/4) */
static bl_tick_t rt_next_bar(bl_tick_t t)
{
    bl_tick_t r = (t - rt.origin) % BL_TICKS_PER_BAR;
    return r ? t + BL_TICKS_PER_BAR - r : t;
}

/* ---- the wire (the tick's) */
static void rt_send(bl_tick_t tick, uint32_t pkt)
{
    uint32_t st = (pkt >> 8) & 0xFFu, n = (pkt >> 16) & 0x7Fu, b = 1u << (n & 31u);
    plat_midi_out(pkt);
    if ((st & 0xF0u) == 0x90u)           /* NoteRegistry.track: an on sounds (whatever its velocity), an off ends it */
        rt.snd[st & 15u][n >> 5] |= b;
    else if ((st & 0xF0u) == 0x80u)
        rt.snd[st & 15u][n >> 5] &= ~b;
#ifdef PLAT_HOST
    host_sent(tick, pkt);
#else
    (void)tick;
#endif
}

/* to send at once, ahead of the queue (under the lock): it carries the sounding tick */
static void rt_now(uint32_t pkt)
{
    rt_ev_t *e;
    if (rt.nn == RT_NOW) {
        rt.dropped++;
        return;
    }
    e = &rt_nq[(rt.nr + rt.nn++) & (RT_NOW - 1u)];
    e->tick = rt.started ? rt.tick : 0;
    e->pkt = pkt;
    e->vid = BL_VID_NONE;
}

/* NoteRegistry.flushEvents (one channel, or ch -1 all), locked: the offs by (channel, note), then CC 123 per channel
 * that had one; the registry lets the notes go as their offs go out */
static void rt_flush(int ch)
{
    uint32_t touched = 0, c, n;
    for (c = 0; c < 16u; c++) {
        if (ch >= 0 && c != (uint32_t)ch)
            continue;
        for (n = 0; n < 128u; n++)
            if (rt.snd[c][n >> 5] >> (n & 31u) & 1u) {
                rt_now(rt_pkt(0x80u | c, n, 0));
                touched |= 1u << c;
            }
    }
    for (c = 0; c < 16u; c++)
        if (touched >> c & 1u)
            rt_now(rt_pkt(0xB0u | c, 123, 0));
}

/* ---- the queue (under the lock) */
static int rt_room(uint32_t n) { return rt.qn + n <= RT_QUEUE; }

static void rt_insert(bl_tick_t tick, uint32_t pkt, uint8_t vid)   /* after those of its tick */
{
    uint32_t j = rt.qn;
    rt_ev_t *e;
    while (j && RT_Q(j - 1u).tick > tick) {
        RT_Q(j) = RT_Q(j - 1u);
        j--;
    }
    e = &RT_Q(j);
    e->tick = tick;
    e->pkt = pkt;
    e->vid = vid;
    rt.qn++;
}

/* out of the queue: the voice's notes on channel ch, which its flush silences (elsewhere they go on: an old
 * engine's offs on the voice's former channel) */
static void rt_purge(uint8_t vid, uint32_t ch)
{
    uint32_t i, w = 0;
    for (i = 0; i < rt.qn; i++) {
        rt_ev_t *e = &RT_Q(i);
        uint32_t st = (e->pkt >> 8) & 0xFFu;
        if (e->vid != vid || (st & 15u) != ch || ((st & 0xF0u) != 0x80u && (st & 0xF0u) != 0x90u))
            RT_Q(w++) = *e;
    }
    rt.qn = (uint16_t)w;
}

/* the transport stops (from the tick or the main loop, locked): Stop (INT), the flush, nothing more to send */
static void rt_halt(void)
{
    rt.playing = 0;
    rt.stop_at = -1;
    rt.ext_run = 0;
    rt.qn = 0;
    if (rt.clock_out)
        rt_now(rt_pkt(0xFCu, 0, 0));
    rt_flush(-1);
}

/* ---- the tick */
static void rt_clock(uint32_t now)       /* the sounding tick, one ms on */
{
    if (rt.ext ? !rt.ext_run || !rt.started : !rt.playing)
        return;
    if (rt.pre) {                        /* INT: tick 0 sounds at the end of the preroll */
        if (--rt.pre == 0)
            rt.started = 1;
        return;
    }
    rt.frac += rt.rate;
    rt.tick += rt.frac / 1000;
    rt.frac %= 1000;
    if (rt.swap_tick >= 0 && rt.tick >= rt.swap_tick) {   /* a tempo's segment: the rest of the ms at its rate */
        int32_t x = (int32_t)(rt.tick - rt.swap_tick) * 1000 + rt.frac;
        x = x * rt.swap_rate / rt.rate;
        rt.tick = rt.swap_tick + x / 1000;
        rt.frac = x % 1000;
        rt.rate = rt.swap_rate;
        rt.tempo_tick = rt.swap_tick;
        rt.swap_tick = -1;
    }
    if (rt.ext && (rt.pulsed || rt.fa) && now - (rt.pulsed ? rt.last_pulse : rt.fa_ms) <= RT_FREEWHEEL_MS
        && rt.tick >= rt.pulse_tick + RT_PULSE) {
        rt.tick = rt.pulse_tick + RT_PULSE - 1;   /* never past the next pulse */
        rt.frac = 999;
    }
}

static void rt_pulse(uint32_t now)       /* EXT: an F8 from the chosen ports */
{
    int gap = rt.npm && now - rt.last_pulse > RT_FREEWHEEL_MS;
    uint32_t i;
    if (!rt.npm || gap)                  /* the rate: over the last beat's pulses, none across a gap */
        rt.npm = 0;
    else if (rt.npm == RT_RING) {
        for (i = 1; i < RT_RING; i++)
            rt.pulse_ms[i - 1u] = rt.pulse_ms[i];
        rt.npm--;
    }
    rt.pulse_ms[rt.npm++] = now;
    if (rt.npm > 1 && now != rt.pulse_ms[0]) {
        rt.rate = (int32_t)(RT_PULSE * 1000u * (rt.npm - 1u) / (now - rt.pulse_ms[0]));
        rt.bpm = (rt.rate + 8) / 16;     /* the followed tempo (Transport's liveBPM = the estimate) */
    }
    if (rt.ext_run) {
        if (!rt.pulsed || now - rt.last_pulse > RT_FREEWHEEL_MS)
            rt.pulse_tick = 0;           /* the first pulse after the start, or after a freewheel: tick 0 ... */
        else
            rt.pulse_tick += RT_PULSE;
        rt.pulsed = 1;
        rt.fa = 0;
        if (rt.tick >= rt.pulse_tick + RT_PULSE) {   /* ... or, the clock past its window (it ran free), the next
                                                      * multiple of 40 from the tick reached: never back */
            bl_tick_t r = rt.tick % RT_PULSE;
            rt.pulse_tick = r ? rt.tick + RT_PULSE - r : rt.tick;
        }
        if (rt.tick < rt.pulse_tick) {
            rt.tick = rt.pulse_tick;
            rt.frac = 0;
        }
    }
    rt.last_pulse = now;
}

/* EXT: tick 0 now, at the tempo in store until the pulses lead (locked). After an FA its first pulse is tick 0 (MIDI's
 * rule): the clock waits for it short of tick 40, RT_FREEWHEEL_MS at most, never ahead of the master */
static void rt_ext_start(int fa, uint32_t now)
{
    rt.pre = 0;
    rt.ext_run = rt.started = 1;
    rt.pulsed = 0;
    rt.fa = (uint8_t)fa;
    rt.fa_ms = now;
    rt.pulse_tick = 0;
    rt.tick = 0;
    rt.frac = 0;
}

static void rt_midi_in(uint32_t now)     /* clock, Start, Continue, Stop (EXT, from the chosen ports); the rest: none */
{
    uint32_t pkt, st;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        st = (pkt >> 8) & 0xFFu;
        if (!rt.ext || !(rt.ext_ports >> port & 1u))
            continue;
        if (st == 0xF8u) {
            rt_pulse(now);
        } else if ((st == 0xFAu || st == 0xFBu) && !rt.playing && !rt.start_req) {   /* (playing: none, as the Mac) */
            rt.start_req = 1;            /* the main loop makes the engine (app_loop) */
            rt_ext_start(1, now);
        } else if (st == 0xFCu) {
            if (rt.playing)
                rt_halt();
            rt.start_req = rt.ext_run = 0;
        }
    }
}

void app_tick(void)
{
    uint32_t now = plat_ms(), room = RT_SEND_MS;
    rt_clock(now);
    rt_midi_in(now);
    for (; room && rt.nn; room--) {      /* what goes out at once, first */
        rt_ev_t *e = &rt_nq[rt.nr];
        rt_send(e->tick, e->pkt);
        rt.nr = (uint16_t)((rt.nr + 1u) & (RT_NOW - 1u));
        rt.nn--;
    }
    if (!rt.playing || !rt.started)
        return;
    for (; room && rt.qn && rt_q[rt.qr].tick <= rt.tick && (rt.stop_at < 0 || rt_q[rt.qr].tick <= rt.stop_at);
         room--) {                       /* then what is due */
        rt_send(rt_q[rt.qr].tick, rt_q[rt.qr].pkt);
        rt.qr = (uint16_t)((rt.qr + 1u) & (RT_QUEUE - 1u));
        rt.qn--;
    }
    if (rt.stop_at >= 0 && rt.tick >= rt.stop_at && (!rt.qn || rt_q[rt.qr].tick > rt.stop_at))
        rt_halt();                       /* the arranger's ending, once all of its tick is out */
}

/* ---- the main loop */
static void rt_channels(const bl_project_t *p)   /* channelMap from a project */
{
    int i;
    for (i = 0; i < BL_NVIDS; i++)
        rt.chan[i] = RT_NONE;
    for (i = 0; i < p->nvoices; i++) {
        int v = bl_vid(p->voice[i].id);
        if (v != BL_VID_NONE)
            rt.chan[v] = p->voice[i].channel & 15u;
    }
}

static void rt_engine_new(void)          /* LiveEngine(project:) of rt_proj */
{
    int i;
    bl_live_init(&rt_eng, &rt_proj);
    rt.fresh = 0;
    for (i = 0; i < rt_eng.project.nvoices; i++)
        rt.vid[i] = (uint8_t)bl_vid(rt_eng.project.voice[i].id);
    rt.stop_queued = 0;
}

static void rt_put(bl_tick_t tick, uint32_t pkt, uint8_t vid)   /* one event into the queue */
{
    uint32_t f = plat_lock();
    if (rt.playing && rt_room(1))
        rt_insert(tick, pkt, vid);
    else if (rt.playing)
        rt.dropped++;
    plat_unlock(f);
}

static void rt_hit(bl_tick_t at, bl_tick_t gate, uint32_t ch, const bl_note_t *q, uint8_t vid)   /* on, then off */
{
    uint32_t f = plat_lock();
    if (rt.playing && rt_room(2)) {
        rt_insert(at, rt_pkt(0x90u | ch, q->midi & 0x7Fu, q->velocity & 0x7Fu), vid);
        rt_insert(at + gate, rt_pkt(0x80u | ch, q->midi & 0x7Fu, 0), vid);
    } else if (rt.playing) {
        rt.dropped += 2;
    }
    plat_unlock(f);
}

/* the window [lo, hi): the engine's notes and CC as MIDI, and the clock (Transport's scheduler loop) */
static void rt_window(bl_tick_t lo, bl_tick_t hi)
{
    int n = bl_live_render(&rt_eng, lo - rt.origin, hi - rt.origin, rt_note, RT_NOTES), m, i, k;
    bl_tick_t t;
    if (n > RT_NOTES) {
        rt.over += (uint32_t)(n - RT_NOTES);
        n = RT_NOTES;
    }
    for (i = 0; i < n; i++) {            /* NoteEventConverter.events */
        const bl_note_t *q = &rt_note[i];
        uint8_t vid = rt.vid[q->voice];
        uint32_t ch = vid == BL_VID_NONE ? RT_NONE : rt.chan[vid];
        bl_tick_t at = q->at + rt.origin, gate = q->gate;
        int r = q->ratchet > 1 ? q->ratchet : 1;
        int32_t slice = RT_STEP / r;
        if ((vid != BL_VID_NONE && rt.muted >> vid & 1u) || ch == RT_NONE)
            continue;
        if (r > 1) {                     /* Ratchet.expand: r hits a slice apart, each its sub-gate */
            bl_tick_t sub = slice * 9 / 10;
            gate = gate < sub ? gate : sub;
            gate = gate > 1 ? gate : 1;
        }
        for (k = 0; k < r; k++)
            rt_hit(at + k * slice, gate, ch, q, vid);
    }
    m = bl_live_render_cc(&rt_eng, lo - rt.origin, hi - rt.origin, rt_cc, RT_CC);
    if (m > RT_CC) {
        rt.over += (uint32_t)(m - RT_CC);
        m = RT_CC;
    }
    for (i = 0; i < m; i++) {            /* ccEvents: never muted */
        const bl_cc_t *c = &rt_cc[i];
        uint8_t vid = rt.vid[c->voice];
        uint32_t ch = vid == BL_VID_NONE ? RT_NONE : rt.chan[vid];
        if (ch != RT_NONE)
            rt_put(c->at + rt.origin, rt_pkt(0xB0u | ch, c->controller & 0x7Fu, c->value & 0x7Fu), vid);
    }
    if (rt.clock_out) {                  /* clockTicks: every 40 ticks */
        t = lo % RT_PULSE;
        for (t = t ? lo + RT_PULSE - t : lo; t < hi; t += RT_PULSE)
            rt_put(t, rt_pkt(0xF8u, 0, 0), BL_VID_NONE);
    }
}

/* the wake's first step: a loaded project takes over when the head is on its bar line (what the old engine has
 * queued still goes out) */
static void rt_take_over(void)
{
    uint32_t f;
    if (!rt.has_next || rt.head != rt_next_bar(rt.head))
        return;
    rt_proj = rt_next;
    rt.has_next = 0;
    rt.origin = rt.head;
    rt_engine_new();
    rt_channels(&rt_proj);
    rt.muted = 0;
    if (!rt.ext && rt_proj.bpm != rt.bpm) {   /* the template's tempo, from its bar line (a pending one binds there
                                               * next, and wins: the Mac's wake has read it already) */
        f = plat_lock();
        rt.swap_tick = rt.head;
        rt.swap_rate = rt_proj.bpm * 16;
        plat_unlock(f);
        rt.bpm = rt_proj.bpm;
    }
}

/* render ahead: windows while the head is less than RT_LOOKAHEAD_MS ahead of the sounding tick */
static void rt_render(void)
{
    for (;;) {
        bl_tick_t now, end, b, stop;
        int32_t rate;
        uint32_t f = plat_lock();
        int on = rt.playing;
        now = rt.started ? rt.tick : 0;
        rate = rt.rate;
        plat_unlock(f);
        if (!on || rt.stop_queued)
            return;
        rt_take_over();                  /* the wake: a loaded project, a tempo, on the head's bar line */
        if (rt.pend_bpm >= 0 && !rt.ext && rt_next_bar(rt.head) == rt.head) {
            f = plat_lock();
            rt.swap_tick = rt.head;      /* the clock's new segment, from there */
            rt.swap_rate = rt.pend_bpm * 16;
            plat_unlock(f);
            rt.bpm = rt.pend_bpm;
            rt.pend_bpm = -1;
        }
        if (rt.head >= now + rate * RT_LOOKAHEAD_MS / 1000)
            return;
        end = rt.head + RT_WINDOW - rt.head % RT_WINDOW;   /* the window: to the next multiple of 240 ... */
#ifdef PLAT_HOST
        if (rt_host_window && (b = rt_host_window(rt.head)) < end)
            end = b;
#endif
        if ((rt.has_next || (rt.pend_bpm >= 0 && !rt.ext)) && (b = rt_next_bar(rt.head)) < end)
            end = b;                     /* ... or the bar line where a change binds */
        if (end <= rt.head)
            return;
        rt_window(rt.head, end);
        rt.head = end;
        stop = bl_live_stop_tick(&rt_eng);   /* the arranger's ending: the transport stops on its tick */
        if (stop >= 0 && rt.head >= stop + rt.origin) {
            f = plat_lock();
            rt.stop_at = stop + rt.origin;
            plat_unlock(f);
            rt.stop_queued = 1;
        }
    }
}

/* ---- the controls (main loop) */
void rt_init(void)                       /* the Full ensemble until the saved project (project.c) gives one */
{
    memset(&rt, 0, sizeof rt);
    rt.pend_bpm = -1;
    rt.swap_tick = rt.tempo_tick = rt.stop_at = -1;
    rt.ext_ports = 1u << PORT_USB | 1u << PORT_TRS;
    bl_template(BL_TPL_FULL, &rt_proj);
    rt_channels(&rt_proj);
    rt.bpm = rt_proj.bpm;
    rt.rate = rt.bpm * 16;
    rt.fresh = 1;
}

void rt_play(void)                       /* Transport.play: always from tick 0, a fresh engine */
{
    uint32_t f;
    if (rt.playing)
        return;
    rt_engine_new();
    rt.head = rt.origin = 0;
    rt.has_next = 0;
    rt.clock_out = !rt.ext;
    f = plat_lock();
    rt.qn = 0;
    rt.swap_tick = rt.stop_at = -1;
    if (!rt.ext) {
        rt.rate = rt.bpm * 16;
        rt.tick = 0;
        rt.frac = 0;
        rt.pre = RT_PREROLL_MS;
        rt.started = 0;
    } else if (!rt.start_req) {          /* PLAY in EXT: tick 0 now (an FA has started the clock already) */
        rt_ext_start(0, plat_ms());
    }
    rt.start_req = 0;
    rt.playing = 1;
    if (rt.clock_out)
        rt_insert(0, rt_pkt(0xFAu, 0, 0), BL_VID_NONE);   /* Start, at tick 0 */
    plat_unlock(f);
    rt_render();
}

void rt_stop(void)                       /* Transport.stop */
{
    uint32_t f = plat_lock();
    rt.has_next = 0;
    if (rt.playing)
        rt_halt();
    rt.start_req = rt.ext_run = 0;
    plat_unlock(f);
    if (rt.pend_bpm >= 0) {              /* (the Mac binds it on the next Play's first bar line: tick 0) */
        rt.bpm = rt.pend_bpm;
        rt.pend_bpm = -1;
    }
}

int rt_playing(void) { return rt.playing; }

bl_tick_t rt_tick(void)                  /* the tick sounding now (0 before tick 0) */
{
    uint32_t f = plat_lock();
    bl_tick_t t = rt.playing && rt.started ? rt.tick : 0;
    plat_unlock(f);
    return t;
}

bl_tick_t rt_head(void) { return rt.head; }
const bl_live_t *rt_engine(void) { return &rt_eng; }

/* Transport.captureState: the running engine's state at the render head (stopped, as its last Play left it); before
 * a Play of the project in place (one loaded stopped: the Mac's new transport), its preview, the initial state */
void rt_capture(bl_state_t *out)
{
    if (rt.fresh)
        bl_initial_state(&rt_proj, out);
    else
        bl_live_capture(&rt_eng, out);
}
int rt_muted(int vid) { return vid >= 0 && vid < BL_NVIDS && rt.muted >> vid & 1u; }
int32_t rt_bpm(void) { return rt.bpm; }

/* Transport.enqueue, bound at the render head at once (the Mac's next wake, before it renders): 0, or -1 when the
 * engine's queue is full and it is dropped. Stopped, nothing (Play starts with no actions) */
int rt_action(const bl_action_t *a)
{
    if (!rt.playing)
        return 0;
    rt_take_over();
    return bl_live_enqueue(&rt_eng, a, rt.head - rt.origin);
}

void rt_set_tempo(int32_t bpm)           /* Transport.setTempo: playing, from the head's next bar line (INT) */
{
    uint32_t f;
    bpm = clamp(bpm, 40, 220);
    if (rt.playing) {
        rt.pend_bpm = bpm;
        return;
    }
    f = plat_lock();
    rt.bpm = bpm;                        /* stopped: the next Play's (EXT: until the pulses lead) */
    if (rt.ext && !rt.ext_run)
        rt.rate = bpm * 16;
    plat_unlock(f);
}

/* playing: Transport.loadProject (from the head's next bar line); stopped: the project the next Play makes (the Mac
 * builds its transport anew: its channels, no mutes, its tempo) */
void rt_load(const bl_project_t *p)
{
    if (rt.playing) {
        rt_next = *p;
        rt.has_next = 1;
        return;
    }
    rt_proj = *p;
    rt_channels(&rt_proj);
    rt.muted = 0;
    rt.bpm = rt_proj.bpm;
    rt.pend_bpm = -1;
    rt.fresh = 1;
}

/* Transport.setMuted, voice a BL_VID_: from the next window; muting silences what the voice's channel sounds (and
 * what the queue holds of the voice's notes) */
void rt_set_mute(int vid, int on)
{
    uint32_t f;
    if (vid < 0 || vid >= BL_NVIDS)
        return;
    rt.muted = (uint8_t)(on ? rt.muted | 1u << vid : rt.muted & ~(1u << vid));
    if (!on)
        return;
    if (rt.chan[vid] == RT_NONE)         /* (no channel: nothing of it to silence; an old engine's offs go on) */
        return;
    f = plat_lock();
    rt_purge((uint8_t)vid, rt.chan[vid]);
    rt_flush(rt.chan[vid]);
    plat_unlock(f);
}

/* Transport.setChannel, voice a BL_VID_, ch 0-15: the running project's, a pending one's; playing, the old channel
 * silenced (and the voice's notes the queue holds) */
static void rt_chan_of(bl_project_t *p, int vid, uint8_t ch)
{
    int i;
    for (i = 0; i < p->nvoices; i++)
        if (bl_vid(p->voice[i].id) == vid)
            p->voice[i].channel = ch;
}
void rt_set_channel(int vid, uint8_t ch)
{
    uint32_t f;
    uint8_t old;
    if (vid < 0 || vid >= BL_NVIDS || ch > 15u || (old = rt.chan[vid]) == RT_NONE || old == ch)
        return;
    rt.chan[vid] = ch;
    rt_chan_of(&rt_proj, vid, ch);
    if (rt.has_next)
        rt_chan_of(&rt_next, vid, ch);
    if (!rt.playing)
        return;
    f = plat_lock();
    rt_purge((uint8_t)vid, old);
    rt_flush(old);
    plat_unlock(f);
}

void rt_set_sync(int ext)                /* INT 0, EXT 1: stopped only */
{
    uint32_t f;
    if (rt.playing || rt.start_req)
        return;
    f = plat_lock();
    rt.ext = ext ? 1 : 0;
    rt.npm = rt.pre = 0;
    rt.rate = rt.bpm * 16;
    plat_unlock(f);
}

void rt_set_ext_port(int mask)           /* EXT's input: bits 1 << PORT_USB, 1 << PORT_TRS */
{
    uint32_t f = plat_lock();
    rt.ext_ports = (uint8_t)(mask & (1 << PORT_USB | 1 << PORT_TRS));
    plat_unlock(f);
}

void app_loop(void)
{
    if (rt.start_req && !rt.playing) {   /* an FA / FB (the tick's): the engine is the main loop's to make */
        proj_ready();                    /* (the project's, before any Play: app.h) */
        rt_play();
    }
    rt_render();
}
