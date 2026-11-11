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
 * - Stop: MIDI Stop (INT), then explicit offs for Tangram-held notes. Mute releases only the selected voice's
 *   current strikes; channel edits leave old strikes and echo tails alone. No channel-wide CC 123 is needed.
 *   The arranger's ending stops at its tick. The registry holds what was handed to plat_midi_out.
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
 * - Lookahead prepares editable original notes. At dispatch each original secures its concrete off and captures
 *   up to four immutable echo pairs in the same tick queue, never recursively. Live edits touch only unsent originals.
 * - The registry tracks each key's latest strike, voice and end tick. A retrigger sends its old off first;
 *   scheduled offs that do not match the current strike's end are discarded, including across voices/projects. */
#include <stddef.h>
#include "runtime.h"

#define RT_WINDOW 240                   /* the windows' grid, in ticks: the longest window */
#define RT_LOOKAHEAD_MS 100
#define RT_PREROLL_MS 100                /* Transport's prerollNanos */
#define RT_QUEUE 1024                    /* timed events (a power of two) */
#define RT_NOW 256                       /* events to send at once: Stop, a flush (a power of two) */
#define RT_SEND_MS 16                    /* packets sent a ms at most */
#define RT_DRAIN_MS 5                    /* after the last packet: the USB-MIDI out ring gone (16 a 1 ms USB frame) */
#define RT_NOTES 128                     /* a window's notes and CC (240 ticks of the templates: at most 9 and 8) */
#define RT_CC 128
#define RT_PULSE 40                      /* ticks per MIDI clock: 960 / 24 */
#define RT_STEP (BL_PPQN / 4)            /* Ratchet.stepTicks: a ratchet's hits share one 16th */
#define RT_FREEWHEEL_MS 500
#define RT_RING 25                       /* the pulse times the EXT rate is measured over: one beat */
#define RT_NONE 0xFFu                    /* the channel of a voice the running project lacks */

enum { RT_EV_MIDI, RT_EV_ORIGINAL, RT_EV_ORIGINAL_OFF, RT_EV_ECHO, RT_EV_ECHO_OFF };

typedef struct {
    bl_tick_t tick;
    bl_tick_t end;                       /* note-on's paired end tick; other events zero */
    uint32_t pkt;                        /* a USB-MIDI packet (plat.h) */
    uint8_t vid;                         /* the voice's BL_VID_ (BL_VID_NONE: the clock, a flush) */
    uint8_t kind;
    bl_delay_t delay;                    /* editable prospective settings on an unsent original only */
    uint32_t source_onset;               /* visual source tick + 1, carried only by originals */
    uint32_t source_grid;
} rt_ev_t;

static bl_live_t rt_eng PLAT_POOL;       /* the running engine */
static bl_project_t rt_proj PLAT_POOL;   /* Transport.project: the next Play's, the running engine's */
static bl_project_t rt_next PLAT_POOL;   /* pendingProject: takes over at the head's next bar line */
/* Small transient render buffers fit in normal RAM; reserve pool headroom for BLE. */
static bl_note_t rt_note[RT_NOTES];
static bl_cc_t rt_cc[RT_CC];
static rt_ev_t rt_q[RT_QUEUE] PLAT_POOL;
static rt_ev_t rt_nq[RT_NOW] PLAT_POOL;  /* to send at once, before the queue */
#define RT_Q(i) rt_q[(rt.qr + (i)) & (RT_QUEUE - 1u)]

static struct {
    /* the main loop's */
    bl_tick_t head, origin;              /* the render head; where the running engine's tick 0 sits */
    uint8_t has_next, stop_queued, ext, clock_out, out_ports;
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
    uint32_t sent_ms;                    /* the last packet's ms */
    bl_tick_t tick;                      /* the sounding tick (started), and its thousandths */
    int32_t frac, rate;                  /* rate: thousandths of a tick per ms (INT: bpm * 16) */
    bl_tick_t swap_tick, tempo_tick;     /* a tempo's clock segment from swap_tick (-1 none); where the last began */
    bl_tick_t stop_at;                   /* the arranger's ending: the transport stops there (-1 none) */
    int32_t swap_rate;
    bl_tick_t pulse_tick;                /* EXT: the last pulse's tick */
    uint32_t last_pulse, fa_ms, pulse_ms[RT_RING];   /* its ms; the FA's; the latest pulses' ms (npm of them) */
    uint8_t snd[16][128];                /* 0 silent, 1 sounding, 2 release requested */
    uint8_t owner[16][128];              /* current strike's voice, including echoes */
    bl_tick_t until[16][128];            /* current strike's scheduled end; rejects stale offs */
    uint16_t nrelease, release_cursor;   /* bounded registry flush, drained within the MIDI packet budget */
    struct {
        uint32_t onset;                 /* latest dispatched original's source tick + 1; zero hides it */
        uint8_t channel, pitch;
        uint32_t event_onset;           /* last original's position survives its Note-Off for the UI */
        uint32_t serial;
        uint32_t grid;
    } source[BL_NVIDS];
} rt;
static uint32_t rt_source_serial;        /* does not restart at Play, unlike the note registry */

/* Display accounting only. No MIDI queue entries or engine state. One cue per
 * rendered bar at most: EXT's existing 24-interval/ms rate bound and 100 ms
 * lookahead cover fewer than 27 bars, so 32 slots include the partial window.
 * Packed parallel arrays avoid padding each timestamp/section pair to 16 bytes. */
#define RT_COUNT_CUES 32
#define RT_COUNT_ON 8u
static struct {
    bl_tick_t total, part, at[RT_COUNT_CUES];
    uint32_t seconds;
    uint16_t ms;
    uint8_t section, composing, rendered, qr, qn;
    uint8_t cue[RT_COUNT_CUES];
} rt_count;

static void rt_count_clear(void)
{
    memset(&rt_count, 0, sizeof rt_count);
    rt_count.section = rt_count.rendered = 0xFFu;
}

static void rt_count_apply(uint8_t cue)
{
    rt_count.composing = (cue & RT_COUNT_ON) != 0;
    if (rt_count.composing) {
        uint8_t section = cue & 7u;
        if (section != rt_count.section) rt_count.part = 0;
        rt_count.section = section;
    }
}

/* Tick context: split even a clock-pulse jump at every due section boundary.
 * Counters use sounding time, never the engine's future rendered bar count. */
static void rt_count_advance(bl_tick_t lo, bl_tick_t hi)
{
    if (hi < lo) return;
    while (rt_count.qn && rt_count.at[rt_count.qr] <= hi) {
        bl_tick_t at = rt_count.at[rt_count.qr];
        if (at > lo) {
            rt_count.total += at - lo;
            if (rt_count.composing) rt_count.part += at - lo;
            lo = at;
        }
        rt_count_apply(rt_count.cue[rt_count.qr]);
        rt_count.qr = (uint8_t)((rt_count.qr + 1u) % RT_COUNT_CUES);
        rt_count.qn--;
    }
    rt_count.total += hi - lo;
    if (rt_count.composing) rt_count.part += hi - lo;
}

/* Main loop, after rendering the window beginning at at. Windows cannot cross
 * a bar, so the final Composer section is the one starting at this window. */
static void rt_count_window(bl_tick_t at)
{
    uint8_t cue = rt_eng.composer.section | (rt_eng.project.composer.enabled ? RT_COUNT_ON : 0);
    uint32_t f = plat_lock();
    if (rt.playing && cue != rt_count.rendered && rt_count.qn < RT_COUNT_CUES) {
        int i = (rt_count.qr + rt_count.qn) % RT_COUNT_CUES;
        rt_count.at[i] = at;
        rt_count.cue[i] = cue;
        rt_count.qn++;
        rt_count.rendered = cue;
    }
    plat_unlock(f);
}

#ifdef PLAT_HOST
static void host_sent(bl_tick_t tick, uint32_t pkt);   /* host.c: dumpmidi */
static bl_tick_t (*rt_host_window)(bl_tick_t head);   /* host.c: a test's hook at each window's start (its actions
                                                         * and mutes); a window ends at its answer at the latest */
static void (*rt_host_start)(void);      /* host.c: a test's tick (the interrupt) just before a start takes the lock */
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
/* Raw wire send. Scheduled notes pass through rt_dispatch below; release requests
 * use this directly. Every held key represents exactly the latest strike. */
static void rt_send(bl_tick_t tick, uint32_t pkt)
{
    uint32_t st = (pkt >> 8) & 0xFFu, n = (pkt >> 16) & 0x7Fu;
    uint8_t *k = &rt.snd[st & 15u][n];
    /* Notes, CC and internally generated Clock/Start/Stop share one output route. */
    plat_midi_send(pkt, rt.out_ports);
    rt.sent_ms = plat_ms();
    if ((st & 0xF0u) == 0x90u)
        *k = 1;
    else if ((st & 0xF0u) == 0x80u) {
        if (*k == 2) rt.nrelease--;
        *k = 0;
        /* Includes real releases, flushes and retrigger offs. Discarded stale
         * offs never reach this function; echo ons never create a marker. */
        for (int vid = 0; vid < BL_NVIDS; vid++)
            if (rt.source[vid].channel == (st & 15u) && rt.source[vid].pitch == n)
                rt.source[vid].onset = 0;
    }
#ifdef PLAT_HOST
    host_sent(tick, pkt);
#else
    (void)tick;
#endif
}

/* A retrigger is two packets, reserved together in this ms's output budget.
 * Older offs may coincide with the new end; releasing then is also correct.
 * Earlier/later stale offs cannot terminate a newer strike. */
static rt_ev_t *rt_insert(bl_tick_t tick, uint32_t pkt, uint8_t vid);
static void rt_echoes(const rt_ev_t *original, uint32_t ch);

static int rt_dispatch(uint32_t *room)
{
    /* Take a value copy: inserting the paired off/echoes may move ring entries.
     * Do not remove the head until enough wire budget exists for its retrigger. */
    rt_ev_t e = RT_Q(0);
    uint32_t st = (e.pkt >> 8) & 0xFFu, ch = st & 15u, n = (e.pkt >> 16) & 0x7Fu;
    int discard = 0;
    if (e.kind == RT_EV_ORIGINAL && (e.vid >= BL_NVIDS || (rt.muted >> e.vid & 1u) || e.end <= e.tick))
        discard = 1;
    if ((st & 0xF0u) == 0x80u && (!rt.snd[ch][n] || rt.until[ch][n] != e.tick ||
        (e.vid != BL_VID_NONE && rt.owner[ch][n] != e.vid))) discard = 1;
    if (!discard && *room < ((st & 0xF0u) == 0x90u && rt.snd[ch][n] ? 2u : 1u)) return 0;
    rt.qr = (uint16_t)((rt.qr + 1u) & (RT_QUEUE - 1u));
    rt.qn--;
    if (discard) return 1;
    if ((st & 0xF0u) == 0x90u) {
        if (e.kind == RT_EV_ORIGINAL) {
            /* Popping this original guarantees one free slot. Secure its off
             * before emitting any on; optional echoes cannot consume that slot. */
            rt_insert(e.end, rt_pkt(0x80u | ch, n, 0), e.vid)->kind = RT_EV_ORIGINAL_OFF;
        }
        if (rt.snd[ch][n]) {
            rt_send(e.tick, rt_pkt(0x80u | ch, n, 0));
            --*room;
        }
        rt_send(e.tick, e.pkt);
        rt.until[ch][n] = e.end;
        rt.owner[ch][n] = e.vid;
        if (e.kind == RT_EV_ORIGINAL) {
            rt.chan[e.vid] = (uint8_t)ch;
            rt.source[e.vid].onset = e.source_onset;
            rt.source[e.vid].channel = (uint8_t)ch;
            rt.source[e.vid].pitch = (uint8_t)n;
            rt.source[e.vid].event_onset = e.source_onset;
            rt.source[e.vid].grid = e.source_grid;
            if (!++rt_source_serial) ++rt_source_serial;
            rt.source[e.vid].serial = rt_source_serial;
            rt_echoes(&e, ch);           /* capture only now; echoes never enter this branch */
        }
    } else {
        rt_send(e.tick, e.pkt);
    }
    --*room;
    return 1;
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
    e->end = 0;
    e->pkt = pkt;
    e->vid = BL_VID_NONE;
    e->kind = RT_EV_MIDI;
    e->delay = (bl_delay_t){0};
    e->source_onset = 0;
    e->source_grid = 0;
}

/* Request explicit offs, bounded by the registry rather than the immediate
 * event ring. Voice-local flushes never send channel-wide All Notes Off. */
static void rt_flush_owned(int vid, int ch)
{
    uint32_t c, n;
    for (c = 0; c < 16u; c++) {
        if (ch >= 0 && c != (uint32_t)ch)
            continue;
        for (n = 0; n < 128u; n++)
            if (rt.snd[c][n] == 1 && (vid < 0 || rt.owner[c][n] == (uint8_t)vid)) {
                rt.snd[c][n] = 2;
                rt.nrelease++;
            }
    }
}

/* ---- the queue (under the lock) */
static int rt_room(uint32_t n) { return rt.qn + n <= RT_QUEUE; }

static rt_ev_t *rt_insert(bl_tick_t tick, uint32_t pkt, uint8_t vid)   /* after those of its tick */
{
    uint32_t j = rt.qn;
    rt_ev_t *e;
    while (j && RT_Q(j - 1u).tick > tick) {
        RT_Q(j) = RT_Q(j - 1u);
        j--;
    }
    e = &RT_Q(j);
    e->tick = tick;
    e->end = 0;
    e->pkt = pkt;
    e->vid = vid;
    e->kind = RT_EV_MIDI;
    e->delay = (bl_delay_t){0};
    e->source_onset = 0;
    e->source_grid = 0;
    rt.qn++;
    return e;
}

/* Remove queued note pairs belonging to a voice: one channel, or RT_NONE for
 * every channel (mute also removes old project/channel echo tails). */
static void rt_purge(uint8_t vid, uint32_t ch)
{
    uint32_t i, w = 0;
    for (i = 0; i < rt.qn; i++) {
        rt_ev_t *e = &RT_Q(i);
        uint32_t st = (e->pkt >> 8) & 0xFFu;
        if (e->vid != vid || (ch != RT_NONE && (st & 15u) != ch) ||
            ((st & 0xF0u) != 0x80u && (st & 0xF0u) != 0x90u))
            RT_Q(w++) = *e;
    }
    rt.qn = (uint16_t)w;
}

/* the transport stops (from the tick or the main loop, locked): Stop (INT), the flush, nothing more to send */
static void rt_halt(void)
{
    /* Keep final counter statistics/fractions until a newly accepted PLAY. */
    rt.playing = 0;
    rt.stop_at = -1;
    rt.ext_run = 0;
    rt.qn = 0;
    if (rt.clock_out)
        rt_now(rt_pkt(0xFCu, 0, 0));
    rt_flush_owned(-1, -1);
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
    bl_tick_t before = rt.tick;
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
    if (rt.playing) {
        if (++rt_count.ms == 1000) {
            rt_count.ms = 0;
            if (rt_count.seconds < UINT32_MAX) rt_count.seconds++;
        }
        bl_tick_t end = rt.stop_at >= 0 && rt.tick > rt.stop_at ? rt.stop_at : rt.tick;
        rt_count_advance(before, end);
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
            bl_tick_t before = rt.tick;
            rt.tick = rt.pulse_tick;
            rt.frac = 0;
            if (rt.playing) {
                bl_tick_t end = rt.stop_at >= 0 && rt.tick > rt.stop_at ? rt.stop_at : rt.tick;
                rt_count_advance(before, end);
            }
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
    /* Flush before any new timed note, also while stopped. At most 2048 keys
     * can be pending, and repeated mute/stop requests cannot duplicate them. */
    while (room && rt.nrelease) {
        uint32_t key = rt.release_cursor;
        rt.release_cursor = (uint16_t)((key + 1u) & 2047u);
        if (rt.snd[key >> 7][key & 127u] != 2) continue;
        rt_send(rt.started ? rt.tick : 0, rt_pkt(0x80u | (key >> 7), key & 127u, 0));
        room--;
    }
    if (!rt.playing || !rt.started)
        return;
    for (; room && rt.qn && rt_q[rt.qr].tick <= rt.tick && (rt.stop_at < 0 || rt_q[rt.qr].tick <= rt.stop_at);) {
        if (!rt_dispatch(&room)) break;
    }
    if (rt.stop_at >= 0 && rt.tick >= rt.stop_at && (!rt.qn || rt_q[rt.qr].tick > rt.stop_at))
        rt_halt();                       /* the arranger's ending, once all of its tick is out */
}

/* ---- the main loop */
static void rt_channels(const bl_project_t *p)   /* channelMap from a project */
{
    uint32_t f = plat_lock();
    int i;
    for (i = 0; i < BL_NVIDS; i++)
        rt.chan[i] = RT_NONE;
    for (i = 0; i < p->nvoices; i++) {
        int v = bl_vid(p->voice[i].id);
        if (v != BL_VID_NONE)
            rt.chan[v] = p->voice[i].channel & 15u;
    }
    plat_unlock(f);
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

/* Under the lock: reject a complete pair on overflow, never leave an orphan on. */
static void rt_echo_pair(bl_tick_t at, bl_tick_t gate, uint32_t ch, uint8_t pitch, uint8_t velocity, uint8_t vid)
{
    if (rt.playing && rt_room(2)) {
        rt_ev_t *on = rt_insert(at, rt_pkt(0x90u | ch, pitch & 0x7Fu, velocity), vid);
        on->end = at + gate;
        on->kind = RT_EV_ECHO;
        rt_insert(at + gate, rt_pkt(0x80u | ch, pitch & 0x7Fu, 0), vid)->kind = RT_EV_ECHO_OFF;
    } else if (rt.playing) {
        rt.dropped += 2;
    }
}

/* Exact positive half-up rounding; compute from the original velocity rather
 * than recursively rounding earlier echoes. 64-bit products stay out of line
 * (FM-1 CPU constraint). At four echoes the denominator is at most 100^4. */
static __attribute__((noinline)) uint8_t rt_echo_velocity(uint8_t velocity, uint8_t decay, int index)
{
    uint64_t numerator = velocity;
    uint32_t denominator = 1;
    for (int i = 0; i < index; i++) {
        numerator *= decay;
        denominator *= 100u;
    }
    uint32_t v = (uint32_t)((numerator + denominator / 2u) / denominator);
    return (uint8_t)(v > 127 ? 127 : v); /* caller skips zero before clamping */
}

/* The tick calls this once an original has actually been handed to MIDI.
 * Echo packets are immutable captures; their delay metadata is never read. */
static void rt_echoes(const rt_ev_t *original, uint32_t ch)
{
    bl_delay_t d = bl_delay_config(original->delay);
    if (d.enabled) {
        int32_t spacing = d.time * RT_STEP;
        uint8_t pitch = (uint8_t)((original->pkt >> 16) & 0x7Fu);
        uint8_t original_velocity = (uint8_t)((original->pkt >> 24) & 0x7Fu);
        for (int echo = 1; echo <= d.feedback / 25; echo++) {
            uint8_t velocity = rt_echo_velocity(original_velocity, d.decay, echo);
            if (!velocity) continue;
            rt_echo_pair(original->tick + echo * spacing, original->end - original->tick,
                         ch, pitch, velocity, original->vid);
        }
    }
}

/* Lookahead holds one prospective original, not a captured off/echo family. */
static void rt_hit(bl_tick_t at, bl_tick_t gate, uint32_t ch, const bl_note_t *q, uint8_t vid)
{
    uint32_t f = plat_lock();
    if (rt.playing && rt_room(1)) {
        rt_ev_t *e = rt_insert(at, rt_pkt(0x90u | ch, q->midi & 0x7Fu, q->velocity & 0x7Fu), vid);
        e->end = at + gate;
        e->kind = RT_EV_ORIGINAL;
        e->delay = bl_delay_config(q->delay);
        e->source_onset = q->source_onset;
        e->source_grid = q->source_grid;
    } else if (rt.playing) rt.dropped++;
    plat_unlock(f);
}

/* the window [lo, hi): the engine's notes and CC as MIDI, and the clock (Transport's scheduler loop) */
static void rt_window(bl_tick_t lo, bl_tick_t hi)
{
    int n = bl_live_render(&rt_eng, lo - rt.origin, hi - rt.origin, rt_note, RT_NOTES), m, i, k;
    bl_tick_t t;
    rt_count_window(lo);
    if (n > RT_NOTES) {
        rt.over += (uint32_t)(n - RT_NOTES);
        n = RT_NOTES;
    }
    for (i = 0; i < n; i++) {            /* NoteEventConverter.events */
        const bl_note_t *q = &rt_note[i];
        uint8_t vid = rt.vid[q->voice];
        uint32_t ch = q->channel;
        bl_tick_t at = q->at + rt.origin, gate = q->gate;
        int r = q->ratchet > 1 ? q->ratchet : 1;
        int32_t slice = RT_STEP / r;
        if (vid == BL_VID_NONE || (rt.muted >> vid & 1u) || ch > 15u)
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
        uint32_t ch = vid == BL_VID_NONE ? RT_NONE : rt_eng.st[c->voice].voice.midi_channel;
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
    rt_count_clear();
    rt.pend_bpm = -1;
    rt.swap_tick = rt.tempo_tick = rt.stop_at = -1;
    rt.ext_ports = 1u << PORT_USB | 1u << PORT_TRS;
    rt.out_ports = TO_USB | TO_BLE;
    bl_template(BL_TPL_FULL, &rt_proj);
    rt_channels(&rt_proj);
    rt.bpm = rt_proj.bpm;
    rt.rate = rt.bpm * 16;
    rt.fresh = 1;
}

/* Transport.play: always from tick 0, a fresh engine. fa: app_loop's, for an FA / FB, which an FC the tick took
 * since its look (the interrupt before the lock here) has taken back: then nothing starts */
static void rt_start(int fa)
{
    uint32_t f;
    if (rt.playing)
        return;
    rt_engine_new();
    rt.head = rt.origin = 0;
    rt.has_next = 0;
    rt.clock_out = !rt.ext;
#ifdef PLAT_HOST
    if (rt_host_start)
        rt_host_start();
#endif
    f = plat_lock();
    if (fa && !rt.start_req) {
        plat_unlock(f);
        return;
    }
    rt.qn = 0;
    rt_count_clear();
    rt.swap_tick = rt.stop_at = -1;
    memset(rt.source, 0, sizeof rt.source);
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

void rt_play(void) { rt_start(0); }

void rt_stop(void)                       /* Transport.stop */
{
    uint32_t f = plat_lock();
    rt.has_next = 0;
    if (rt.playing)
        rt_halt();
    rt.start_req = rt.ext_run = 0;
    plat_unlock(f);
    if (rt.pend_bpm >= 0) {              /* (the Mac binds it on the next Play's first bar line: tick 0) */
        if (!rt.ext) rt.bpm = rt.pend_bpm;
        rt.pend_bpm = -1;
    }
}

int rt_playing(void) { return rt.playing; }

/* Scalar out-of-line division avoids the FM-1 CPU's unsafe inlined 64-bit
 * arithmetic patterns near state loads. Copy shared ticks under the lock. */
static __attribute__((noinline)) uint32_t rt_count_bars(bl_tick_t ticks)
{
    bl_tick_t bars = ticks / BL_TICKS_PER_BAR;
    return bars > UINT32_MAX ? UINT32_MAX : (uint32_t)bars;
}

rt_counters_t rt_counters(void)
{
    uint32_t f = plat_lock();
    uint32_t seconds = rt_count.seconds;
    bl_tick_t total = rt_count.total, part = rt_count.part;
    plat_unlock(f);
    return (rt_counters_t){seconds, rt_count_bars(part), rt_count_bars(total)};
}

/* stopped, and what the stop sends (MIDI Stop, the flush) gone out, RT_DRAIN_MS since: a flash write, which stops
 * everything for a moment, may come now */
int rt_idle(void)
{
    uint32_t f = plat_lock();
    int r = !rt.playing && !rt.nn && !rt.nrelease && plat_ms() - rt.sent_ms >= RT_DRAIN_MS;
    plat_unlock(f);
    return r;
}

bl_tick_t rt_tick(void)                  /* the tick sounding now (0 before tick 0) */
{
    uint32_t f = plat_lock();
    bl_tick_t t = rt.playing && rt.started ? rt.tick : 0;
    plat_unlock(f);
    return t;
}

bl_tick_t rt_head(void) { return rt.head; }

bl_tick_t rt_play_tick(void)             /* the tick sounding now in the running engine's ticks (a template that took
                                          * over at a bar line: from its origin), 0 before them */
{
    bl_tick_t t = rt_tick() - rt.origin;
    return t < 0 ? 0 : t;
}

uint32_t rt_source_onset(int vid)
{
    if (vid < 0 || vid >= BL_NVIDS) return 0;
    uint32_t f = plat_lock();
    uint32_t onset = rt.playing && !(rt.muted >> vid & 1u) ? rt.source[vid].onset : 0;
    plat_unlock(f);
    return onset;
}

uint32_t rt_source_event_grid(int vid, uint32_t *onset, uint32_t *grid)
{
    *onset = 0;
    *grid = 0;
    if (vid < 0 || vid >= BL_NVIDS) return 0;
    uint32_t f = plat_lock();
    uint32_t serial = rt.source[vid].serial;
    if (rt.playing) *onset = rt.source[vid].event_onset;
    if (rt.playing) *grid = rt.source[vid].grid;
    plat_unlock(f);
    return serial;
}

uint32_t rt_source_event(int vid, uint32_t *onset)
{
    uint32_t grid;
    return rt_source_event_grid(vid, onset, &grid);
}

int rt_loading(void) { return rt.playing && rt.has_next; }   /* pendingProject: one waits for its bar line */

int rt_channel(int vid)                  /* channelMap: voice vid's channel 0-15, -1 none */
{
    return vid >= 0 && vid < BL_NVIDS && rt.chan[vid] != RT_NONE ? rt.chan[vid] : -1;
}

const bl_live_t *rt_engine(void) { return &rt_eng; }

int rt_select_composer_stage(int section)
{
    if (section < 0 || section >= 7 || rt_loading()) return -1;
    if (rt.playing) {
        rt_take_over();
        return bl_live_select_composer_stage(&rt_eng, section, rt.head - rt.origin);
    }
    rt_proj.composer_start = (uint8_t)section;
    return 0;                         /* retain stopped material/recall and preview */
}

int rt_set_composer(const bl_compcfg_t *cfg)
{
    if (!cfg || !bl_comp_valid(cfg) || rt_loading()) return -1;
    if (rt.playing && bl_live_set_composer(&rt_eng, cfg, rt.head - rt.origin)) return -1;
    rt_proj.composer = *cfg;
    if (!rt.playing) rt.fresh = 1;
    return 0;
}

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

int rt_has_voice_state(void) { return !rt.fresh; }

int rt_get_voice_config(int vid, bl_vconfig_t *active, bl_vconfig_t *target)
{
    if (rt.playing) rt_take_over();
    else if (rt.fresh) rt_engine_new(); /* existing engine memory doubles as the stopped preview */
    return bl_live_get_config(&rt_eng, vid, active, target);
}

int rt_set_voice_config(int vid, const bl_vconfig_t *target)
{
    if (!target || !bl_config_valid(target)) return -1;
    if (rt.playing) rt_take_over();
    if (rt_loading()) return -1;
    int index = -1;
    for (int i = 0; i < rt_proj.nvoices; i++)
        if (bl_vid(rt_proj.voice[i].id) == vid && vid >= 0 && vid < BL_NVIDS) index = i;
    if (index < 0) return -1;
    if (rt.playing && bl_live_set_config(&rt_eng, vid, target)) return -1;
    rt_proj.voice[index].has_config = 1;
    rt_proj.voice[index].config = *target;
    if (!rt.playing) rt.fresh = 1;
    return 0;
}

int rt_set_voice_config_field(int vid, int field, int32_t value)
{
    bl_vconfig_t target;
    if (rt_get_voice_config(vid, NULL, &target) || bl_config_edit(&target, field, value)) return -1;
    return rt_set_voice_config(vid, &target);
}

static void rt_stored(int32_t bpm)      /* stored musical tempo only controls the INTERNAL clock */
{
    uint32_t f = plat_lock();
    if (!rt.ext) {
        rt.bpm = bpm;
        rt.rate = bpm * 16;
    }
    plat_unlock(f);
}

void rt_set_tempo(int32_t bpm)           /* Transport.setTempo: playing, from the head's next bar line (INT) */
{
    bpm = clamp(bpm, 40, 220);
    if (rt.ext) return;                 /* caller's project BPM is stored independently of the followed clock */
    if (rt.playing)
        rt.pend_bpm = bpm;
    else
        rt_stored(bpm);
}

/* Playing: musical project takeover at the head's next bar. Stopped: the next
 * Play's project/channels. Neither path changes global clock settings or an
 * externally followed tempo estimate. */
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
    rt_stored(rt_proj.bpm);
    rt.pend_bpm = -1;
    rt.fresh = 1;
}

/* Transport.setMuted, voice a BL_VID_: purge its original/echo pairs on all
 * channels and release only its currently owned strikes. Other voices survive. */
void rt_set_mute(int vid, int on)
{
    uint32_t f;
    if (vid < 0 || vid >= BL_NVIDS)
        return;
    f = plat_lock();
    rt.muted = (uint8_t)(on ? rt.muted | 1u << vid : rt.muted & ~(1u << vid));
    if (on) {
        rt_purge((uint8_t)vid, RT_NONE);
        rt_flush_owned(vid, -1);
    }
    plat_unlock(f);
}

int rt_get_delay(int vid, bl_delay_t *out)
{
    if (!out || vid < 0 || vid >= BL_NVIDS) return -1;
    if (rt.playing) rt_take_over();
    else if (rt.fresh) rt_engine_new();
    for (int i = 0; i < rt_eng.project.nvoices; i++)
        if (rt.vid[i] == vid) {
            *out = rt_eng.st[i].voice.delay;
            return 0;
        }
    return -1;
}

int rt_set_delay(int vid, const bl_delay_t *settings)
{
    if (!settings || !bl_delay_valid(*settings) || vid < 0 || vid >= BL_NVIDS) return -1;
    if (rt.playing) rt_take_over();
    if (rt_loading()) return -1;
    int index = -1;
    for (int i = 0; i < rt_proj.nvoices; i++)
        if (bl_vid(rt_proj.voice[i].id) == vid) index = i;
    if (index < 0) return -1;
    rt_proj.voice[index].delay = *settings;
    if (rt.playing) {
        /* Only the main loop owns engine state. The lock makes the queued
         * prospective edits atomic against note dispatch in the interrupt. */
        uint32_t f = plat_lock();
        for (int i = 0; i < rt_eng.project.nvoices; i++) if (rt.vid[i] == vid) {
            rt_eng.st[i].voice.delay = *settings;
            rt_eng.project.voice[i].delay = *settings;
        }
        for (uint32_t i = 0; i < rt.qn; i++)
            if (RT_Q(i).vid == vid && RT_Q(i).kind == RT_EV_ORIGINAL) RT_Q(i).delay = *settings;
        plat_unlock(f);
    } else rt.fresh = 1;
    return 0;
}

int rt_set_delay_field(int vid, int field, int value)
{
    bl_delay_t settings;
    if (rt_get_delay(vid, &settings) || bl_delay_edit(&settings, field, value)) return -1;
    return rt_set_delay(vid, &settings);
}

/* Next-note channel selection: change prospective originals and future source
 * frames only. Sounding notes' offs and already captured echo pairs keep their channels. */
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
    if (vid < 0 || vid >= BL_NVIDS || ch > 15u || rt.chan[vid] == RT_NONE)
        return;
    rt_chan_of(&rt_proj, vid, ch);
    if (rt.has_next)
        rt_chan_of(&rt_next, vid, ch);
    f = plat_lock();
    rt.chan[vid] = ch;
    if (rt.playing) {
        rt_chan_of(&rt_eng.project, vid, ch);
        for (int i = 0; i < rt_eng.project.nvoices; i++)
            if (rt.vid[i] == vid) rt_eng.st[i].voice.midi_channel = ch;
        for (uint32_t i = 0; i < rt.qn; i++)
            if (RT_Q(i).vid == vid && RT_Q(i).kind == RT_EV_ORIGINAL)
                RT_Q(i).pkt = (RT_Q(i).pkt & ~0x0F00u) | (uint32_t)ch << 8;
    } else rt.fresh = 1;
    plat_unlock(f);
}

int rt_apply_clock_settings(int ext, int ports)
{
    if ((ext != 0 && ext != 1) || !ports || (ports & ~(1 << PORT_USB | 1 << PORT_TRS))) return -1;
    uint32_t f = plat_lock();
    if (rt.ext != ext) {
        if (rt.playing || rt.start_req) {
            plat_unlock(f);
            return -1;
        }
        rt.ext = (uint8_t)ext;
        rt.npm = rt.pre = 0;
        if (!ext) {
            rt.bpm = rt_proj.bpm;
            rt.rate = rt.bpm * 16;
        }
    }
    rt.ext_ports = (uint8_t)ports;
    plat_unlock(f);
    return 0;
}

int rt_apply_output_settings(int output_mask)
{
    if (!output_mask || (output_mask & ~(TO_USB | TO_BLE))) return -1;
    uint32_t f = plat_lock();
    if (rt.out_ports != output_mask && (rt.playing || rt.start_req)) {
        plat_unlock(f);
        return -1;
    }
    rt.out_ports = (uint8_t)output_mask;
    plat_unlock(f);
    return 0;
}

void rt_set_sync(int ext)                /* existing callers now edit the authoritative global record */
{
    device_settings_t settings = device_settings_get();
    settings.sync = ext ? DEVICE_SYNC_EXTERNAL : DEVICE_SYNC_INTERNAL;
    device_settings_set(settings);
}

void rt_set_ext_port(int mask)           /* EXT's input: bits 1 << PORT_USB, 1 << PORT_TRS */
{
    device_settings_t settings = device_settings_get();
    settings.ext_ports = (uint8_t)(mask & (1 << PORT_USB | 1 << PORT_TRS));
    device_settings_set(settings);
}

void app_loop(void)
{
    if (rt.start_req && !rt.playing) {   /* an FA / FB (the tick's): the engine is the main loop's to make */
        proj_ready();                    /* (the project's, before any Play: app.h) */
        rt_start(1);
    }
    rt_render();
}
