/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan's screen: the pages, the knob strip, the header and the screen reader; ui.c has the panel, the help
 * cards' text and the loop. Part of the unity build, after ui.c, whose state (ui, the mirror) it reads.
 *
 * The screen is AMB-1's: a header (the template, the key, the tempo; a message for a moment), the page, and the
 * knob strip (the page's four values: a ring or pips where the knob changes it, the value alone where it is shown
 * only). The pages are the Mac's views on 240 x 240 px: PERFORM the voice lanes (VoiceLaneView), the horizon
 * (HorizonStripView), the convergence (ConvergenceView) and the tension left; VOICE and PATTERN the selected voice
 * and its pattern in steps; CC its lanes and the template's scenes; ARRANGER the arranger's phases and settings;
 * DELAY the actual MIDI delay settings; SETUP the root, the clock, the lights and the seed. Playing, they show
 * the running engine (bl_live_status, its patterns); stopped, the transport's preview (the state its next Play
 * starts from, rt_capture) and the project's values. Everything on the screen is told to a screen reader too
 * (announce.c, fm1-reader), in the screen's own words: app/words.tsv says the short ones in full. */

#include <stddef.h>

static const char *const TPL_TITLE[BL_NTPL] = {"String Trio", "Full ensemble", "Ambient drift"};
/* by voice id (BL_VID_ order): the Mac's names (VoiceRole.displayName) and colours (VoiceColor) */
static const char *const VID_NAME[BL_NVIDS] = {"Pluck 1", "Pluck 2", "Bass", "Pad", "Lead", "Heartbeat", "Drone"};
/* the generators (BL_GEN_ order: GeneratorID's raw values, in words) */
static const char *const GEN_NAME[BL_NGENS] = {"SEED PROCEDURE", "PULSING BASS", "STAGED PLUCK", "BASS MELODY", "CHORD LAYER",
                                               "HEARTBEAT", "DRONE", "ARP", "MOTIF"};
static const char *const GEN_SHORT[BL_NGENS] = {"SEED", "PULSE", "STAGED", "BASS", "CHORD", "BEAT", "DRONE", "ARP", "MOTIF"};
static const char *const SCALE_SHORT[BL_NMODES] = {"NAT MIN", "DORIAN", "PHRYG", "MIXOLYD", "LYDIAN", "LYD DOM",
    "HARM MN", "MIN PNT", "LOCRIAN", "MAJOR", "PMAJ", "EGYP", "HIRA", "MELM", "HMAJ", "ALTR", "GYPS",
    "ROMN", "BHAI", "MARV", "PURV", "TODI", "ARAB"};
/* the arranger's phases (ArrangementPhase), as the Mac's panel and horizon label them */
static const char *const PHASE_NAME[7] = {"BUILD", "BED", "VAR", "B-PART", "PAYOFF", "SUBTR", "END"};

#include "ui_theme.h"
/* Semantic aliases only: palette values and role colors are centralized. */
#define K_BG (ui_colors()->background)
#define K_PANEL (ui_colors()->panel)
#define K_STRIP (ui_colors()->strip)
#define K_STEP (ui_colors()->step)
#define K_LINE (ui_colors()->line)
#define K_TEXT (ui_colors()->text)
#define K_TEXT2 (ui_colors()->secondary)
#define K_DIM (ui_colors()->dim)
#define K_DIM2 (ui_colors()->disabled)
#define K_FOCUS (ui_colors()->focus)
#define K_INFO (ui_colors()->info)
#define K_PENDING (ui_colors()->info)
#define K_LOCK (ui_colors()->locked)
#define K_MUTED (ui_colors()->muted)
#define K_SOURCE (ui_colors()->source)
#define K_TIMING (ui_colors()->timing)
#define K_SUPPRESSED (ui_colors()->suppressed)
#define K_INVERSE (ui_colors()->inverse)
enum { SHOW_TIMING_PLAYHEAD = 1 };       /* no user setting */

static bl_state_t ui_pre PLAT_POOL;      /* stopped: the transport's state (its preview, or where its last Play left) */

/* ------------------------------------------------------------ the lanes --- */
/* what the pages show of each voice (LiveEngine.Snapshot.VoiceSnapshot): playing, the running engine's, in its
 * project's order; stopped, the transport's state (rt_capture: the preview of its next Play, or where its last Play
 * left it), every voice in, nothing pending */
typedef struct {
    const bl_pattern_t *pat;
    int64_t gen;
    int16_t remaining;                   /* the lifecycle's verbatim plays left, -1 established */
    uint8_t vid, active, blank, locked, pending;   /* pending a BL_REQ_ */
    uint32_t source_onset;               /* dispatched original, independent of the render-ahead pattern cursor */
} lane_t;
static lane_t lane[BL_MAX_VOICES];
static int nlanes;
static uint32_t source_marker(int vid, const bl_pattern_t *pattern)
{
    if (vid < 0 || vid >= BL_NVIDS) return 0;
    uint32_t onset, grid;
    /* Runtime owns one dispatch latch per BL_VID_, independent of selection,
     * Note-Off, redraw cadence and echo playback. Keep its last source while
     * the displayed grid still matches; never reinterpret an old onset after
     * render-ahead has resized that source. */
    rt_source_event_grid(vid, &onset, &grid);
    return grid == bl_pattern_grid(pattern) ? onset : 0;
}

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* text: s, or v in digits, after d (the caller's d has the room); the end */
static char *cat(char *d, const char *s)
{
    while (*d)
        d++;
    while ((*d = *s++))
        d++;
    return d;
}
static char *cat_u(char *d, uint32_t v)
{
    while (*d)
        d++;
    itoa_u(v, d);
    while (*d)
        d++;
    return d;
}
static uint32_t hash_s(uint32_t h, const char *s)
{
    while (*s)
        h = hash(h, (uint8_t)*s++);
    return hash(h, 0);
}

static uint32_t pre_sig(void)            /* the project the stopped transport holds: its preview changes with it */
{
    uint32_t h = 2166136261u;
    const uint8_t *p = (const uint8_t *)&proj;
    for (uint32_t i = 0; i < sizeof proj; i++) h = hash(h, p[i]);
    return h;
}

static void lanes(void)
{
    int i;
    nlanes = 0;
    if (ui.st_ok) {
        ui.pre_ok = 0;                   /* (at the stop the transport's state is taken again) */
        for (i = 0; i < ui_st.nvoices; i++) {
            lane_t *l = &lane[nlanes++];
            l->pat = &bl_live_voice(rt_engine(), i)->pat;
            l->gen = ui_st.voice[i].generation;
            l->remaining = ui_st.voice[i].remaining;
            l->vid = ui_st.voice[i].vid;
            l->source_onset = source_marker(l->vid, l->pat);
            l->active = ui_st.voice[i].active;
            l->blank = ui_st.voice[i].blank;
            l->locked = ui_st.voice[i].locked;
            l->pending = ui_st.voice[i].pending;
        }
        return;
    }
    if (!ui.pre_ok || ui.pre_sig != pre_sig()) {
        rt_capture(&ui_pre);
        ui.pre_ok = 1;
        ui.pre_sig = pre_sig();
    }
    for (i = 0; i < ui_pre.n; i++) {
        lane_t *l = &lane[nlanes++];
        l->pat = &ui_pre.v[i].pat;
        l->gen = ui_pre.generation[i];
        l->remaining = ui_pre.v[i].pat.remaining;
        l->vid = (uint8_t)bl_vid(ui_pre.v[i].id);
        l->source_onset = 0;
        l->active = 1;
        l->blank = 0;
        l->locked = ui_pre.v[i].pat.locked;
        l->pending = BL_REQ_NONE;
    }
}

static const lane_t *lane_of(int vid)    /* the lane of a voice id, 0 none */
{
    int i;
    for (i = 0; i < nlanes; i++)
        if (lane[i].vid == vid)
            return &lane[i];
    return 0;
}
static const lane_t *sel_lane(void) { return lane_of(key_vid(ui.sel)); }
static const bl_voice_t *voice_material(int vid)
{
    int n = ui.st_ok ? ui_st.nvoices : ui_pre.n;
    for (int i = 0; i < n; i++) {
        const bl_voice_t *v = ui.st_ok ? bl_live_voice(rt_engine(), i) : &ui_pre.v[i];
        if (bl_vid(v->id) == vid) return v;
    }
    return NULL;
}
static const bl_voice_t *sel_material(void) { return voice_material(key_vid(ui.sel)); }
static const bl_vcfg_t *sel_cfg(void)   /* the selected voice in the app project's template, 0 none */
{
    int i = key_voice(ui.sel);
    return i < 0 ? 0 : &tpl_now()->voice[i];
}
static const char *vid_name(int vid) { return vid < BL_NVIDS ? VID_NAME[vid] : "Voice"; }
static uint16_t vid_color(int vid) { return vid >= 0 && vid < BL_NVIDS ? ui_colors()->track[vid] : K_TEXT2; }

/* ----------------------------------------------------------- the values --- */
#define F100 F64(0x4059000000000000)    /* 100.0 */
#define STEP_TICKS (BL_PPQN / 4)         /* a 16th: the Mac's steps (VoiceLaneView.loopSteps) */

static int32_t bpm_now(void) { return rt_bpm(); }   /* liveBPM ?? project.bpm: the transport's, stopped too (EXT's
                                                     * followed tempo, kept by Sync INT, as Transport.currentBPM) */

static void key_now(bl_key_t *k)         /* the key sounding (UIState.liveKey ?? project.key) */
{
    if (ui.st_ok) {
        *k = ui_st.key;
        return;
    }
    k->root = proj.key_root;
    bl_scale_preset(proj.key_mode, &k->scale);
}

static uint32_t pct(f64 x)               /* 0-1 as a percentage, rounded (to the nearest, ties away) */
{
    int64_t v = f64_to_i64(f64_round(f64_mul(x, F100)));
    return v < 0 ? 0u : (uint32_t)v;
}

static int32_t steps(const bl_pattern_t *p) { return (int32_t)(p->length / STEP_TICKS); }

static int32_t timing_position(const bl_pattern_t *p)
{
    return p->length > 0 ? (int32_t)((rt_play_tick() + p->start_offset) % p->length) : 0;
}

/* MIDI note n (0-127) as a name, middle C (60) C3 as in Ableton and Logic (Ruling AE): 0 C-2, 24 C0, 55 G2 */
static void note_name(int n, char *t)
{
    int o = n / 12 - 2;
    t[0] = 0;
    cat(t, NOTE_NAME[n % 12]);
    if (o < 0)
        cat(t, "-");
    cat_u(t, (uint32_t)(o < 0 ? -o : o));
}
/* a register low-high as shown ("G2-G4"), or said ("G2 to G4", "C-2 to C-1": a minus stays the octave's) */
static void reg_text(int lo, int hi, char *t, int words)
{
    char a[8], b[8];
    note_name(lo, a);
    note_name(hi, b);
    t[0] = 0;
    cat(cat(cat(t, a), words ? " to " : "-"), b);
}

/* a note's scale degree as the Pattern page labels it: 1 up to the key's notes (bl_resolve's floor_mod: in minor
 * pentatonic, degree 5 is the next octave's 1) */
static int degree_label(const bl_pnote_t *n, const bl_key_t *k)
{
    return (int)bl_floor_mod(n->pitch.degree, k->scale.count) + 1;
}

static char drum_label(int note)
{
    switch (note) {
    case BL_DRUM_KICK: return 'K';
    case BL_DRUM_SNARE: return 'S';
    case BL_DRUM_CLOSED_HAT: return 'H';
    case BL_DRUM_OPEN_HAT: return 'O';
    default: return '?';
    }
}

/* DelayHint.compute(role:bpm:): the division by role (dotted 8th on the plucks, the heartbeat and aux; 8th on the
 * bass and the drone; quarter on the pad and the lead) and 60000 / bpm x beats ms, Int(.rounded()): to the nearest,
 * ties away. In integers, 60000 x beats over bpm rounded; equal to the Mac's binary64 sums for every tempo 40-220
 * (the host's delay_check) */
enum { DIV_DOTTED8, DIV_8TH, DIV_QUARTER };
static const char *const DIV_NAME[3] = {"dotted 8th", "8th", "quarter"};   /* DelayDivision.label */
static const uint16_t DIV_MS_BEATS[3] = {45000, 30000, 60000};            /* 60000 x 0.75, 0.5, 1.0 */
static __attribute__((unused)) int delay_div(int role)
{
    return role == BL_ROLE_BASS || role == BL_ROLE_DRONE  ? DIV_8TH
           : role == BL_ROLE_PAD || role == BL_ROLE_LEAD ? DIV_QUARTER
                                                          : DIV_DOTTED8;
}
static __attribute__((unused)) uint32_t delay_ms(int div, int32_t bpm)
{
    uint32_t n = DIV_MS_BEATS[div], b = (uint32_t)bpm;
    return (2u * n + b) / (2u * b);
}

/* Convergence.period: the LCM of every lane's loop length, -1 when it overflows Int64 (the Mac's nil, "infinite") */
static int64_t gcd64(int64_t a, int64_t b)
{
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}
static __attribute__((noinline)) int64_t lcm_step(int64_t acc, int64_t len)   /* (acc / g) x len, -1 past Int64 */
{
    int64_t a = acc / gcd64(acc, len);
    return a > INT64_MAX / len ? -1 : a * len;
}
static int64_t convergence(void)
{
    int64_t acc;
    int i;
    if (!nlanes)
        return -1;
    acc = lane[0].pat->length;
    for (i = 1; i < nlanes && acc > 0; i++)
        acc = lcm_step(acc, lane[i].pat->length);
    return acc;
}
/* ConvergenceView's text: the bars (whole, else to a tenth, as %.1f: half to even on the bars' exact k / 16) and
 * the seconds at the tempo shown, rounded; "INF (practically)" for nil */
static void convergence_text(int64_t period, char *t, int words)
{
    int64_t bars = period / BL_TICKS_PER_BAR, d = 16 * (int64_t)bpm_now(), s;
    int32_t rem = (int32_t)(period % BL_TICKS_PER_BAR), q, r;
    t[0] = 0;
    if (period < 0) {
        cat(t, "INF (practically)");
        return;
    }
    q = rem * 10 / BL_TICKS_PER_BAR;
    r = rem * 10 % BL_TICKS_PER_BAR;
    if (2 * r > BL_TICKS_PER_BAR || (2 * r == BL_TICKS_PER_BAR && (q & 1)))
        q++;
    if (q == 10) {
        bars++;
        q = 0;
    }
    cat_u(t, (uint32_t)bars);
    if (rem) {
        cat(t, ".");
        cat_u(t, (uint32_t)q);
    }
    cat(t, bars == 1 && !rem ? " bar" : " bars");
    s = period / d + (2 * (period % d) >= d);   /* Convergence.seconds, rounded */
    cat(t, words ? ", about " : " ~ ");
    cat_u(t, (uint32_t)s);
    cat(t, words ? " seconds" : " s");
}

/* the knob strip's cells: a ring or pips for what the knob changes (the range, the value), the value alone for what
 * is shown (and a second line under it) */
enum { CELL_RING, CELL_PIPS, CELL_TEXT };
typedef struct {
    uint8_t kind;
    int16_t lo, hi, v;
    char val[24], sub[24];
} cell_t;
static const char *const FORM_SHORT[] = {"CUSTOM", "CLASSIC", "SLOW", "PLATEAU"};
static const char *const LIGHT_NAME[3] = {"OFF", "ON", "KEYS"};

/* Host vocabulary enumeration supplies a local display-only preview. */
static const device_settings_t *screen_clock_preview;
static device_settings_t screen_clock(void)
{
    return screen_clock_preview ? *screen_clock_preview : device_settings_get();
}
static int port_index(void)
{
    for (int i = 0; i < 7; i++) if (screen_clock().ext_ports == GLO_EXT_PORTS[i]) return i;
    return 0;
}

static void cycle_text(f64 bars, char *t)   /* a lane's cycle in bars (0.5-64): whole, or to a tenth */
{
    int64_t tenths = f64_to_i64(f64_round(f64_mul(bars, F64(0x4024000000000000))));   /* x 10.0 */
    t[0] = 0;
    cat_u(t, (uint32_t)(tenths / 10));
    if (tenths % 10) {
        cat(t, ".");
        cat_u(t, (uint32_t)(tenths % 10));
    }
}

static int voice_cell(int k, cell_t *c)
{
    if (k < KN_TRACK || k > KN_DECAY) return 0;
    const bl_voice_t *v = sel_material();
    const bl_vconfig_t *cfg = v ? &v->pending : NULL;
    c->kind = CELL_RING;
    c->hi = 1;
    if (k == KN_TRACK) {
        c->hi = NVKEYS - 1;
        c->v = ui.sel;
        cat(c->val, ROLE_NAME[ui.sel]);
        return 1;
    }
    if (!cfg) {
        c->kind = CELL_TEXT;
        cat(c->val, "-");
        return 1;
    }
    int arp = cfg->gen.kind == BL_GEN_ARP || cfg->gen.kind == BL_GEN_MOTIF;
    if (cfg->gen.kind == BL_GEN_HEARTBEAT && k == KN_GATE) {
        c->kind = CELL_TEXT;
        cat(c->val, "50%");
        cat(c->sub, "FIXED");
        return 1;
    }
    if (!arp && (k == KN_RATE || k == KN_GATE || k == KN_DIRECTION || k == KN_GROOVE)) {
        c->kind = CELL_TEXT;
        cat(c->val, "N/A");
        cat(c->sub, "ARP/MOTIF");
        return 1;
    }
    switch (k) {
    case KN_DRUM_PARTS:
        c->kind = CELL_TEXT;
        c->v = (int16_t)bl_drum_parts(cfg->gen.direction);
        cat_u(c->val, (uint32_t)c->v);
        cat(c->sub, (const char *const[]){"", "BD", "BD SD", "BD SD CH", "ALL"}[c->v]);
        break;
    case KN_DRUM_GROOVE:
        c->kind = CELL_TEXT;
        c->v = cfg->gen.groove == BL_DRUM_SPARSE ? 1 : cfg->gen.groove == BL_DRUM_DRIVE ? 3 : 2;
        cat_u(c->val, (uint32_t)c->v);
        cat(c->sub, (const char *const[]){"", "SPARSE", "STEADY", "DRIVE"}[c->v]);
        break;
    case KN_VDENSITY:
        c->hi = 100; c->v = (int16_t)(100 - cfg->density_cut);
        cat_u(c->val, (uint32_t)c->v); cat(c->val, "%"); break;
    case KN_GENERATOR: c->hi = BL_NGENS - 1; c->v = cfg->gen.kind; cat(c->val, GEN_SHORT[c->v]); break;
    case KN_VROOT: c->hi = 11; c->v = (int16_t)cfg->key.root; cat(c->val, NOTE_NAME[c->v]); break;
    case KN_SCALE: c->hi = BL_NMODES - 1; c->v = cfg->key.scale.mode; cat(c->val, SCALE_SHORT[c->v]); break;
    case KN_DIRECTION:
        c->kind = CELL_PIPS; c->hi = BL_NDIRS - 1; c->v = cfg->gen.direction;
        cat(c->val, DIR_LABEL[c->v]); break;
    case KN_GROOVE:
        c->kind = CELL_PIPS; c->hi = BL_NGROOVES - 1; c->v = cfg->gen.groove;
        cat(c->val, GROOVE_LABEL[c->v]); break;
    case KN_RATE:
        c->kind = CELL_PIPS; c->v = cfg->gen.a == 240;
        cat(c->val, c->v ? "1/16" : "1/8"); break;
    case KN_GATE: {
        c->lo = 1; c->hi = cfg->gen.a - 1; c->v = cfg->gen.b;
        int tenths = (cfg->gen.b * 1000 + cfg->gen.a / 2) / cfg->gen.a;
        cat_u(c->val, (uint32_t)(tenths / 10));
        if (tenths % 10) cat_u(cat(c->val, "."), (uint32_t)(tenths % 10));
        cat(c->val, "%"); break;
    }
    case KN_LOOP:
        c->lo = 1; c->hi = 16; c->v = (int16_t)clamp((int32_t)(cfg->length / STEP_TICKS), 1, 16);
        cat_u(c->val, cfg->length / STEP_TICKS); cat(c->sub, "STEPS"); break;
    case KN_PHASE:
        c->hi = (int16_t)(bl_config_phase_count(cfg) - 1); c->v = cfg->phase;
        if (!c->hi) c->kind = CELL_TEXT;
        cat_u(c->val, cfg->phase); break;
    case KN_REGISTER:
        c->hi = 127 - (cfg->reg_high - cfg->reg_low); c->v = cfg->reg_low;
        if (!c->hi) c->kind = CELL_TEXT;
        reg_text(cfg->reg_low, cfg->reg_high, c->val, 0); break;
    case KN_DELAY_ON:
        c->kind = CELL_PIPS; c->v = v->delay.enabled;
        cat(c->val, c->v ? "ON" : "OFF"); break;
    case KN_DELAY_TIME:
        c->hi = 6; c->v = v->delay.time - 1;
        cat(c->val, TIME_LABEL[c->v]); break;
    case KN_FEEDBACK:
        c->kind = CELL_PIPS; c->hi = 3; c->v = v->delay.feedback / 25 - 1;
        cat_u(c->val, v->delay.feedback); cat(c->val, "%"); break;
    case KN_DECAY:
        c->lo = 50; c->hi = 100; c->v = v->delay.decay;
        cat_u(c->val, v->delay.decay); cat(c->val, "%"); break;
    default: break;
    }
    return 1;
}

static void cell(int view, int i, cell_t *c)
{
    const bl_vcfg_t *vc = sel_cfg();
    const lane_t *l = sel_lane();
    int k = view_knob(view, i), v = key_voice(ui.sel);
    memset(c, 0, sizeof *c);
    c->kind = k < KN_SHOWN ? CELL_PIPS : CELL_TEXT;
    c->hi = 1;
    if (voice_cell(k, c)) return;
    switch (k) {
    case KN_PERF_SLOT: {
        char name[PERFORMANCE_PRESET_NAME_BYTES + 1];
        int used = performance_preset_used(ui.perf_slot + 1);
        c->kind = CELL_TEXT;
        if (ui.perf_slot < 9) cat(c->val, "0");
        cat_u(c->val, ui.perf_slot + 1u);
        performance_name(name);
        cat(c->sub, used < 0 ? "ERROR" : !used && ui.perf_phase != PERF_EDIT ? "EMPTY" : name);
        break;
    }
    case KN_PERF_OP:
        c->kind = CELL_TEXT;
        cat(c->val, PERF_OP_NAME[ui.perf_op]);
        break;
    case KN_PERF_POS:
        c->kind = CELL_TEXT;
        if (ui.perf_phase != PERF_EDIT) cat(c->val, "-");
        else if (ui.perf_pos == PERFORMANCE_PRESET_NAME_BYTES) cat(c->val, "END");
        else cat_u(c->val, ui.perf_pos + 1u);
        break;
    case KN_PERF_CHAR:
        c->kind = CELL_TEXT;
        if (ui.perf_phase != PERF_EDIT) cat(c->val, "-");
        else if (ui.perf_char == PERF_BACKSPACE) {
            cat(c->val, "BS"); cat(c->sub, "SEL DEL");
        } else if (PERF_CHARS[ui.perf_char] == ' ') {
            cat(c->val, "_"); cat(c->sub, "SPACE");
        } else {
            c->val[0] = PERF_CHARS[ui.perf_char]; c->val[1] = 0;
        }
        break;
    case KN_THEME:
        c->kind = CELL_TEXT;
        cat(c->val, DEVICE_THEME_NAME[device_settings_get().theme]);
        cat(c->sub, "GLOBAL"); break;
    case KN_DEVICE_RETRY: {
        int status = device_settings_status();
        c->kind = CELL_TEXT;
        cat(c->val, status < 0 ? "RETRY" : status ? "PENDING" : "SAVED");
        if (status < 0) cat(c->sub, "TURN TO RETRY");
        break;
    }
    case KN_COMPOSER:
        c->v = proj.composer.enabled;
        cat(c->val, c->v ? "ON" : "OFF");
        cat(c->sub, "NEXT BAR"); break;
    case KN_COMP_BARS:
        c->kind = CELL_RING; c->hi = 16; c->v = proj.composer.bars / 4;
        if (proj.composer.bars) cat_u(c->val, proj.composer.bars);
        else cat(c->val, "AUTO");
        cat(c->sub, "PHRASE UNIT"); break;
    case KN_COMP_EVOLUTION:
        c->hi = 3; c->v = proj.composer.evolution;
        cat(c->val, (const char *const[]){"OFF", "GENTLE", "MOVING", "ACTIVE"}[c->v]);
        cat(c->sub, "COMPOSER ONLY"); break;
    case KN_COMP_TENSION:
        c->hi = 3; c->v = proj.composer.tension;
        cat(c->val, (const char *const[]){"HOME", "WARM", "LIFT", "PEAK"}[c->v]);
        cat(c->sub, "B SECTION"); break;
    case KN_NONE:
        c->kind = CELL_TEXT;
        cat(c->val, "-");
        break;
    case KN_TEMPO:
        c->kind = CELL_RING;
        c->lo = 40, c->hi = 220, c->v = (int16_t)proj.bpm;
        cat_u(c->val, (uint32_t)proj.bpm);
        break;
    case KN_AUTO:
        c->v = proj.arr_on;
        cat(c->val, proj.arr_on ? "ON" : "OFF");
        break;
    case KN_FORM:
        c->lo = FORM_CLASSIC, c->hi = FORM_AMBIENT, c->v = (int16_t)proj_form();   /* (none or custom: no pip) */
        cat(c->val, proj.arr_has ? FORM_SHORT[proj_form()] : "NONE");
        break;
    case KN_AMBIENT:
        c->v = proj.arr_ambient;
        cat(c->val, proj.arr_ambient ? "ON" : "OFF");
        break;
    case KN_CHANNEL:
        c->kind = CELL_RING;
        c->hi = 15, c->v = v < 0 ? -1 : sel_material() ? sel_material()->midi_channel : proj.channel[v];
        cat(c->val, "-");
        if (v >= 0)
            itoa_u((uint32_t)c->v + 1u, c->val);
        break;
    case KN_ROOT:
        c->kind = CELL_RING;
        c->hi = 11, c->v = proj.key_root;
        cat(c->val, NOTE_NAME[proj.key_root]);
        break;
    case KN_SYNC:
        c->v = screen_clock().sync;
        cat(c->val, screen_clock().sync ? "EXT" : "INT");
        break;
    case KN_PORT:
        c->hi = 6, c->v = (int16_t)port_index();
        cat(c->val, GLO_EXT_NAMES[c->v]);
        break;
    case KN_MIDI_OUT:
    case KN_CLOCK_OUT: {
        uint8_t mask = k == KN_MIDI_OUT ? screen_clock().midi_out : screen_clock().clock_out;
        c->hi = 2, c->v = (int16_t)(mask - DEVICE_MIDI_USB);
        cat(c->val, DEVICE_MIDI_NAME[c->v]);
        break;
    }
    case KN_LIGHTS:
        c->hi = 2, c->v = screen_clock().lights;
        cat(c->val, LIGHT_NAME[screen_clock().lights % 3u]);
        break;
    case KNS_LOOP:
    case KNS_STEPS:
        cat(c->val, "-");
        if (l)
            itoa_u((uint32_t)steps(l->pat), c->val);
        if (k == KNS_LOOP)
            cat(c->sub, "STEPS");
        else if (l && l->pat->length % BL_TICKS_PER_BAR == 0) {
            cat_u(c->sub, (uint32_t)(l->pat->length / BL_TICKS_PER_BAR));
            cat(c->sub, l->pat->length == BL_TICKS_PER_BAR ? " BAR" : " BARS");
        }
        break;
    case KNS_REGISTER:
        cat(c->val, "-");
        if (vc) {
            reg_text(vc->reg_low, vc->reg_high, c->val, 0);
            cat_u(c->sub, vc->reg_low);
            cat(c->sub, "-");
            cat_u(c->sub, vc->reg_high);
        }
        break;
    case KNS_GEN:                        /* its first word, then the rest */
        cat(c->val, "-");
        if (vc) {
            const char *g = GEN_NAME[vc->gen.kind], *sp = g;
            int n;
            while (*sp && *sp != ' ')
                sp++;
            n = (int)(sp - g) < 23 ? (int)(sp - g) : 23;
            memcpy(c->val, g, (uint32_t)n);
            c->val[n] = 0;
            cat(c->sub, *sp ? sp + 1 : "");
        }
        break;
    case KNS_TARGET:
        cat(c->val, "NONE");
        if (vc && vc->nlanes) {
            int t = vc->lane[0].target;
            c->val[0] = 0;
            if (t < vc->ncc) {
                caps(c->val, vc->cc_name[t], sizeof c->val);
                cat(c->sub, "CC ");
                cat_u(c->sub, vc->cc_num[t]);
            } else {
                cat(c->val, "UNMAPPED");
            }
        }
        break;
    case KNS_CYCLE:
        cat(c->val, "-");
        if (vc && vc->nlanes) {
            cycle_text(vc->lane[0].cycle_bars, c->val);
            cat(c->sub, f64_eq(vc->lane[0].cycle_bars, F64(0x3FF0000000000000)) ? "BAR" : "BARS");
        }
        break;
    case KNS_DEPTH:
        cat(c->val, "-");
        if (vc && vc->nlanes) {
            c->val[0] = 0;
            cat_u(c->val, pct(vc->lane[0].depth));
            cat(c->val, "%");
            cat(c->sub, "AT ");
            cat_u(c->sub, pct(vc->lane[0].center));
            cat(c->sub, "%");
        }
        break;
    case KNS_SCENE: {
        int s = scene_now();
        if (s != BL_SCN_NONE)
            caps(c->val, BL_SCN_NAME[s], sizeof c->val);
        else
            cat(c->val, "NONE");
        if (tpl_now()->nscenes) {
            cat_u(c->sub, tpl_now()->nscenes);
            cat(c->sub, " SCENES");
        } else {
            cat(c->sub, "NO SCENES");
        }
        break;
    }
    case KNS_PHASE:
        if (!proj.arr_has && !(ui.st_ok && ui_st.has_arr)) {
            cat(c->val, "NONE");
        } else if (ui.st_ok && ui_st.has_arr) {
            cat(c->val, PHASE_NAME[ui_st.phase % 7u]);
            cat_u(c->sub, (uint32_t)ui_st.bars_in_phase);
            cat(c->sub, ui_st.bars_in_phase == 1 ? " BAR IN" : " BARS IN");
        } else {
            cat(c->val, "-");
            cat(c->sub, "STOPPED");
        }
        break;
    case KNS_LENGTH:
        if (proj.arr_has) {
            cat_u(c->val, (uint32_t)proj.arr_min);
            cat(c->val, "-");
            cat_u(c->val, (uint32_t)proj.arr_max);
            cat(c->sub, "BARS");
        } else {
            cat(c->val, "NONE");
        }
        break;
    case KNS_DROP:                       /* Project.bPart: nil in every template, BPartConfig() keeps the bed */
        cat(c->val, "OFF");
        cat(c->sub, "B PART");
        break;
    case KNS_REPRISE:
        cat(c->val, proj.arr_has && proj.arr_reprise ? "ON" : "OFF");
        break;
    case KN_RATCHET:                     /* the project's, whichever voice is selected */
        c->kind = CELL_RING;
        c->hi = 50, c->v = proj.ratchet_pct;
        ratchet_text(proj.ratchet_pct, c->val);
        break;
    case KNS_GENERATION:
        cat(c->val, "-");
        if (l)
            itoa_u((uint32_t)l->gen, c->val);
        break;
    case KNS_PLAYS:
        cat(c->val, "-");
        if (l) {
            itoa_u(l->remaining > 0 ? (uint32_t)l->remaining : 0u, c->val);
            cat(c->sub, l->locked ? "LOCKED" : l->remaining < 0 ? "ESTABLISHED" : "");
        }
        break;
    case KNS_FEEDBACK:                   /* DelayHint's own: feedback, wet 0.5, ping-pong, filtered repeats */
    case KNS_WET:
        cat(c->val, "50%");
        break;
    case KNS_PINGPONG:
        cat(c->val, "ON");
        break;
    case KNS_FILTERED:
        cat(c->val, "ON");
        cat(c->sub, "REPEATS");
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------- screen --- */
/* three bands (AMB-1's): the header (28 px), the page (144), the knob strip (68), each drawn into the canvas and
 * sent when what it shows has changed */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68

/* a rectangle with rounded corners (radius r, up to 6) */
static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}
static void text_r(int32_t xr, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(xr - text_w(f, s), y, f, s, c);
}
/* the largest of f, FONT_XS and FONT_T in which s fits w px (lower case as capitals in FONT_T) */
static const felucca_font_t *fit(const felucca_font_t *f, const char *s, int32_t w)
{
    if (text_w(f, s) <= w)
        return f;
    return text_w(&FONT_XS, s) <= w ? &FONT_XS : &FONT_T;
}

/* Single-line text with explicit pixel bounds. Used only by the replaced
 * Tangram pages; legacy LFO/GLO retain their existing font/layout path.
 * Shrink first, then truncate with dots if even the smallest font is too wide. */
static void text_box(int32_t x, int32_t y, int32_t w, int32_t h,
                     const felucca_font_t *preferred, const char *s, uint16_t color, int align)
{
    if (w <= 0 || h <= 0) return;
    const felucca_font_t *fonts[4] = {preferred, &FONT_B, &FONT_XS, &FONT_T};
    const felucca_font_t *f = &FONT_T;
    char t[64];
    int n = 0;
    while (s[n] && n < (int)sizeof t - 4) { t[n] = s[n]; n++; }
    t[n] = 0;
    for (int i = 0; i < 4; i++)
        if (fonts[i]->h <= h && text_w(fonts[i], t) <= w) {
            f = fonts[i];
            break;
        }
    if (f->h > h) return;
    if (text_w(f, t) > w || s[n]) {
        do {
            if (n) n--;
            t[n] = '.'; t[n + 1] = '.'; t[n + 2] = '.'; t[n + 3] = 0;
        } while (n && text_w(f, t) > w);
        if (text_w(f, t) > w) return;
    }
    int32_t width = text_w(f, t);
    if (align == 1) x += (w - width) / 2;
    else if (align == 2) x += w - width;
    cv_text(x, y + (h - f->h) / 2, f, t, color);
}

static uint16_t mix(uint16_t a, uint16_t b, int t)   /* t 0..16: a -> b */
{
    int r, g, bl;
    if (t <= 0)
        return a;
    if (t >= 16)
        return b;
    r = ((a >> 11) * (16 - t) + (b >> 11) * t) / 16;
    g = (((a >> 5) & 63) * (16 - t) + ((b >> 5) & 63) * t) / 16;
    bl = ((a & 31) * (16 - t) + (b & 31) * t) / 16;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* anti-aliased drawing (AMB-1's, in integers: no floating point in the firmware): a pixel blended over the canvas (a
 * 0..16), a disc and a knob's ring; distances in 1/16 px, angles in 4096ths of a turn */
static void px_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *q;
    uint32_t bg;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1 || !a)
        return;
    q = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    if (a >= 16u) {
        *q = swap16(c);
        return;
    }
    bg = swap16(*q);
    *q = swap16((uint32_t)(((((c >> 11) * a + (bg >> 11) * (16u - a)) >> 4) << 11) |
                           (((((c >> 5) & 63u) * a + ((bg >> 5) & 63u) * (16u - a)) >> 4) << 5) |
                           (((c & 31u) * a + (bg & 31u) * (16u - a)) >> 4)));
}

static uint32_t isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

/* sin of a (4096ths of a turn) x 1024: Bhaskara I's approximation on each half turn (within 0.2 %) */
static int32_t isin(int32_t a)
{
    int32_t p = a & 4095, s = 1, q;
    if (p >= 2048) {
        p -= 2048;
        s = -1;
    }
    q = p * (2048 - p);
    return s * ((q * 1024) / ((20971520 - 4 * q) >> 4));
}
static int32_t icos(int32_t a) { return isin(a + 1024); }

/* the angle of (x, y) in 4096ths of a turn from +x, clockwise on screen (y down): atan on [0, 1] as
 * pi/4 r + 0.273 r (1 - r) */
static int32_t iangle(int32_t y, int32_t x)
{
    int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, n = ax < ay ? ax : ay, d = ax < ay ? ay : ax, a;
    if (!d)
        return 0;
    a = 512 * n / d + 178 * n * (d - n) / (d * d);
    if (ay > ax)
        a = 1024 - a;
    if (x < 0)
        a = 2048 - a;
    if (y < 0)
        a = 4096 - a;
    return a;
}

static void disc16(int32_t cx, int32_t cy, int32_t r, uint16_t c)   /* centre and radius in 1/16 px */
{
    int32_t x0 = (cx - r) / 16 - 1, x1 = (cx + r) / 16 + 1, y0 = (cy - r) / 16 - 1, y1 = (cy + r) / 16 + 1, x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int32_t dx = x * 16 + 8 - cx, dy = y * 16 + 8 - cy, a = r - (int32_t)isqrt((uint32_t)(dx * dx + dy * dy)) + 8;
            px_blend(x, y, c, (uint32_t)(a < 0 ? 0 : a > 16 ? 16 : a));
        }
}

/* a knob (AMB-1's): a 270-degree track from 7:30 to 4:30, the value v of 1024 filled from the start, round ends
 * and a round handle */
#define RING_A0 1536                     /* 7:30, in 4096ths of a turn from 3:00 */
#define RING_SWEEP 3072
static void knob_ring(int32_t cx, int32_t cy, int32_t r, int32_t v, uint16_t track, uint16_t fill, uint16_t bg)
{
    int32_t ro = r * 16, ri = ro - 54, rm = ro - 27, x, y, c16x = cx * 16 + 8, c16y = cy * 16 + 8, lim;
    v = v < 0 ? 0 : v > 1024 ? 1024 : v;
    lim = v * RING_SWEEP / 1024;
    for (y = -r - 1; y <= r + 1; y++)
        for (x = -r - 1; x <= r + 1; x++) {
            int32_t d = (int32_t)isqrt((uint32_t)(x * x + y * y) * 256u), a, b, t;
            a = ro + 8 - d;
            b = d - ri + 8;
            a = (a < 0 ? 0 : a > 16 ? 16 : a) * (b < 0 ? 0 : b > 16 ? 16 : b) / 16;
            if (a <= 0)
                continue;
            t = (iangle(y, x) - RING_A0) & 4095;
            if (t > RING_SWEEP)
                continue;
            px_blend(cx + x, cy + y, t <= lim && v > 0 ? fill : track, (uint32_t)a);
        }
    {
        int32_t e0 = RING_A0, e1 = RING_A0 + RING_SWEEP, ev = RING_A0 + lim;
        disc16(c16x + icos(e0) * rm / 1024, c16y + isin(e0) * rm / 1024, 27, v > 0 ? fill : track);
        disc16(c16x + icos(e1) * rm / 1024, c16y + isin(e1) * rm / 1024, 27, v >= 1024 ? fill : track);
        disc16(c16x + icos(ev) * rm / 1024, c16y + isin(ev) * rm / 1024, 48, bg);
        disc16(c16x + icos(ev) * rm / 1024, c16y + isin(ev) * rm / 1024, 37, fill);
    }
}

/* Status text on a black surface; state color belongs to the foreground. */
static int32_t tag(int32_t x, int32_t y, int32_t h, const char *s, uint16_t c)
{
    int32_t w = text_w(&FONT_T, s) + 6;
    cv_round(x, y, w, h, 2, K_BG);
    cv_text(x + 3, y + (h - 12) / 2, &FONT_T, s, c);
    return w;
}

/* a horizon chip (HorizonStripView's): a, a dot, b, on black, FONT_XS; its width, nothing drawn when
 * draw is 0 */
static int32_t chip(int32_t x, int32_t y, const char *a, const char *b, uint16_t c, int draw)
{
    int32_t wa = text_w(&FONT_XS, a), w = wa + 10 + (b && b[0] ? 9 + text_w(&FONT_XS, b) : 0);
    if (!draw)
        return w;
    cv_round(x, y, w, 15, 6, K_BG);
    cv_text(x + 5, y, &FONT_XS, a, c);
    if (b && b[0]) {
        cv_rect(x + 5 + wa + 3, y + 7, 2, 2, c);
        cv_text(x + 5 + wa + 9, y, &FONT_XS, b, c);
    }
    return w;
}

/* Display-only mask over the stored onset grid. Direction maps pitches before
 * density in the renderer, but neither density ranking nor groove uses pitch.
 * Use the same filters on private scratch; never compact the displayed source. */
static void visible_onsets(const bl_pattern_t *p, uint8_t audible[BL_MAX_NOTES])
{
    static bl_pattern_t filtered PLAT_POOL;
    memset(audible, 1, BL_MAX_NOTES);
    f64 density = ui.st_ok ? ui_st.density :
        proj.has_density ? pct_of(proj.density_pct) : F64(0x3FF0000000000000); /* 1.0 */
    if (ui.st_ok) {
        const bl_live_t *engine = rt_engine();
        for (int i = 0; i < engine->project.nvoices; i++)
            if (&engine->st[i].voice.pat == p) { density = bl_live_voice_density(engine, i); break; }
    } else {
        for (int i = 0; i < ui_pre.n; i++) if (&ui_pre.v[i].pat == p && ui_pre.v[i].config.density_cut)
            density = f64_mul(density, pct_of(100 - ui_pre.v[i].config.density_cut));
    }
    bl_density_cap(p, density, &filtered);
    int kept = 0;                       /* density compaction retains source order */
    for (int i = 0; i < p->n; i++) {
        audible[i] = 0;
        if (kept >= filtered.n) continue;
        const bl_pnote_t *a = &p->note[i], *b = &filtered.note[kept];
        /* Position alone cannot distinguish chord tones sharing an onset.
         * Match the retained subsequence by values, without comparing padding. */
        if (a->position != b->position || a->gate != b->gate || a->pitch.degree != b->pitch.degree ||
            a->pitch.octave != b->pitch.octave || a->pitch.accidental != b->pitch.accidental ||
            a->velocity != b->velocity || a->accent != b->accent || a->ratchet != b->ratchet) continue;
        uint8_t accent = b->accent;
        audible[i] = (uint8_t)bl_groove_onset(&filtered, b->position, &accent);
        kept++;
    }
}

static void strip_timing_marker(int32_t x, int32_t y, int32_t w, int32_t h, const lane_t *l)
{
    if (!SHOW_TIMING_PLAYHEAD || !ui.st_ok || !l->active || l->pat->length <= 0) return;
    /* Local loop time supplies the wrap; each 16-step window has the same
     * spacing even when the persistent source marker is on another page. */
    int32_t pos = timing_position(l->pat) % BL_TICKS_PER_BAR;
    cv_rect(x + pos * w / BL_TICKS_PER_BAR, y, 1, h, K_TIMING);
}

static void strip_source_marker(int32_t x, int32_t y, int32_t w, int32_t h, const lane_t *l)
{
    if (!ui.st_ok || !l->source_onset || l->source_onset - 1 >= l->pat->length) return;
    int32_t pos = (l->source_onset - 1) % BL_TICKS_PER_BAR;
    int32_t sx = x + pos * w / BL_TICKS_PER_BAR, mw = 2;
    if (sx + mw > x + w) mw = x + w - sx;
    /* Keep semantic red distinct from FIRE's red/orange note graphics. */
    int32_t left = sx > x ? sx - 1 : x;
    int32_t right = sx + mw < x + w ? sx + mw + 1 : x + w;
    cv_rect(left, y, right - left, h, K_BG);
    cv_rect(sx, y, mw, h, K_SOURCE);
}

/* a pattern strip (PatternStripView): the notes by position, length and pitch (self-scaling to the
 * pattern's range, in the key at MIDI 60, the Mac's referenceC), the accents' tops tangerine, the voice's own
 * playhead (its loop position: the phasing, visible); dimmed when muted, waiting or blanked */
static void strip(int32_t x, int32_t y, int32_t w, int32_t h, const lane_t *l, uint16_t c, int dim)
{
    const bl_pattern_t *p = l->pat;
    int32_t len = (int32_t)p->length, lo = 127, hi = 0, span, nh = h >= 24 ? 4 : 2, i, j;
    /* Every lane uses a 16-step window, including shorter loops. Longer
     * material pages by its selected source, or by local time during silence. */
    int32_t at = ui.st_ok ? timing_position(p) : 0;
    int32_t focus = l->source_onset ? (int32_t)(l->source_onset - 1u) : at;
    int32_t page = focus / BL_TICKS_PER_BAR * BL_TICKS_PER_BAR;
    int32_t pitch[BL_MAX_NOTES];
    uint8_t audible[BL_MAX_NOTES];
    bl_key_t k;
    uint16_t nc = dim || l->blank ? mix(K_STRIP, c, 6) : c;
    cv_round(x, y, w, h, 2, K_STRIP);
    if (len <= 0)
        return;
    visible_onsets(p, audible);
    key_now(&k);
    if (p->generator == BL_GEN_HEARTBEAT) { lo = BL_DRUM_KICK; hi = BL_DRUM_OPEN_HAT; }
    for (i = 0; i < p->n; i++) {
        pitch[i] = p->generator == BL_GEN_HEARTBEAT ? p->note[i].pitch.degree :
                   bl_resolve(&k, p->note[i].pitch, 60);
        lo = pitch[i] < lo ? pitch[i] : lo;
        hi = pitch[i] > hi ? pitch[i] : hi;
    }
    lo -= 2;
    hi += 2;
    span = hi - lo < 5 ? 5 : hi - lo;
    for (i = 0; i < p->n; i++) {
        int32_t pos = (int32_t)p->note[i].position, g = p->note[i].gate < len ? (int32_t)p->note[i].gate : len;
        bl_tick_t end_tick = p->note[i].position - page + g;
        if (pos - page >= BL_TICKS_PER_BAR || end_tick <= 0) continue;
        int32_t begin = pos < page ? 0 : pos - page;
        int32_t end = end_tick > BL_TICKS_PER_BAR ? BL_TICKS_PER_BAR : (int32_t)end_tick;
        int32_t nx = x + begin * w / BL_TICKS_PER_BAR, nw = (end - begin) * w / BL_TICKS_PER_BAR;
        int32_t ny = y + 1 + (hi - pitch[i]) * (h - 2 - nh) / span;
        nw = nw < 2 ? 2 : nx + nw > x + w ? x + w - nx : nw;
        if (audible[i]) {
            cv_rect(nx, ny, nw, nh, nc);
        } else {
            /* Keep the source bar's geometry, but distinguish suppression by
             * shape and neutral colour rather than a subtle tint reduction. */
            uint16_t muted = dim || l->blank ? mix(K_STRIP, K_SUPPRESSED, 8) : K_SUPPRESSED;
            for (j = 0; j < nw; j += 4)
                cv_rect(nx + j, ny, nw - j < 2 ? nw - j : 2, nh, muted);
        }
        if (audible[i] && p->note[i].accent && !dim)
            cv_rect(nx, ny, nw, 1, K_FOCUS);
    }
    if (ui.st_ok) {
        strip_timing_marker(x, y, w, h, l);
        strip_source_marker(x, y, w, h, l);
    }
}

/* the legitimization countdown (LifecycleDotsView): filled, the verbatim plays left (up to 4; amber when locked);
 * all empty, established (mutable): cyan rings */
static void dots(int32_t x, int32_t y, const lane_t *l)
{
    int32_t j, n = l->remaining > 4 ? 4 : l->remaining > 0 ? l->remaining : 0;
    for (j = 0; j < 4; j++) {
        if (j < n) {
            cv_round(x + j * 6, y, 4, 4, 1, l->locked ? K_LOCK : K_TEXT2);
        } else {
            cv_round(x + j * 6, y, 4, 4, 1, n ? K_DIM2 : mix(K_BG, K_INFO, 9));
            cv_rect(x + j * 6 + 1, y + 1, 2, 2, K_BG);
        }
    }
}

/* the voice's marks, the most telling first: WAITS (the arranger holds it back), MUTE, LOCK, BLANK; n of them */
static int marks(const lane_t *l, const char *m[4], uint16_t c[4])
{
    int n = 0;
    if (!l->active)
        m[n] = "WAITS", c[n++] = K_FOCUS;
    if (rt_muted(l->vid))
        m[n] = "MUTE", c[n++] = K_MUTED;
    if (l->locked)
        m[n] = "LOCK", c[n++] = K_LOCK;
    if (l->blank)
        m[n] = "BLANK", c[n++] = K_INFO;
    return n;
}

static int lane_dim(const lane_t *l) { return rt_muted(l->vid) || !l->active; }

/* ---- the header: the template in a tag, its key; on the right the tempo, a beat light (a play triangle while
 * stopped) and a dot while a SAVE waits for the stop. Messages take its place for a moment */
static void draw_header(void)
{
    char t[16];
    int msg = plat_ms() < ui.msg_until, play = rt_playing();
    int beat = play && rt_play_tick() % BL_PPQN < BL_PPQN / 2;
    uint32_t h = hash(hash(2166136261u, proj.tpl | (uint32_t)play << 4 | (uint32_t)msg << 5 | (uint32_t)screen_clock().sync << 6 |
                                            (uint32_t)beat << 7 | (uint32_t)proj_save_pending() << 8 |
                                            (uint32_t)proj.key_root << 12 | (uint32_t)proj.key_mode << 16),
                      (uint32_t)bpm_now());
    if (msg)
        h = hash_s(hash_s(hash(h, ui.msg_until), ui.msg[0]), ui.msg[1]);
    h = hash(h, device_settings_get().theme);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], K_FOCUS);
    } else {
        int32_t x = 232, w;
        const felucca_font_t *f;
        if (proj_save_pending()) {       /* a SAVE waiting for the stop */
            cv_round(x - 5, 12, 5, 5, 2, K_FOCUS);
            x -= 11;
        }
        if (screen_clock().sync)
            caps(t, "EXT", sizeof t);
        else
            itoa_u((uint32_t)bpm_now(), t);
        x -= text_w(&FONT_S, t);
        cv_text(x, 6, &FONT_S, t, play ? K_TEXT : K_DIM);
        x -= 16;
        if (play) {                      /* the beat */
            cv_round(x, 9, 10, 10, 5, beat ? K_FOCUS : mix(K_BG, K_FOCUS, 5));
        } else {
            int32_t j;                   /* stopped: a play triangle */
            for (j = 0; j < 5; j++)
                cv_rect(x + 2 + j, 9 + j, 1, 11 - 2 * j, K_DIM);
        }
        w = text_w(&FONT_B, TPL_TITLE[proj.tpl]) + 12;   /* the template, in a tag */
        cv_round(4, 4, w, 20, 6, K_TEXT);
        cv_text(10, 6, &FONT_B, TPL_TITLE[proj.tpl], K_INVERSE);
        w = cv_text(w + 10, 8, &FONT_XS, NOTE_NAME[proj.key_root], K_TEXT);
        f = fit(&FONT_XS, MODE_NAME[proj.key_mode], x - 6 - (w + 4));
        if (w + 4 + text_w(f, MODE_NAME[proj.key_mode]) <= x - 6)   /* (no room: the Setup page has it) */
            cv_text(w + 4, f == &FONT_T ? 10 : 8, f, MODE_NAME[proj.key_mode], K_DIM);
    }
    cv_blit(0, 0);
}

/* ---- PERFORM: the voice lanes (VoiceLaneView: the identity edge, the name, the channel, the pattern strip with
 * its playhead, the loop's steps, the lifecycle dots; WAITS / MUTE / LOCK / BLANK), the horizon (HorizonStripView:
 * the pending degree, the arranger's countdown, the bar-bound changes coming, the nearest mutation), the convergence
 * (ConvergenceView: the LCM of the loops) with the degree in force and the tension left */
#define LANES_H 86
#define HZ_MAX 11
typedef struct {
    char a[24], b[24];
    uint16_t c;
} hz_t;

static void bars_away(bl_tick_t tick, char *t)   /* HorizonStripView.barsAway, without its " · " */
{
    bl_tick_t now = rt_play_tick(), away = (tick - (tick < now ? tick : now)) / BL_TICKS_PER_BAR;
    t[0] = 0;
    if (!ui.st_ok || away <= 1) {
        cat(t, "next bar");
        return;
    }
    cat(t, "in ");
    cat_u(t, (uint32_t)away);
    cat(t, " bars");
}
static void bar_count(int32_t n, char *t)   /* "1 bar", "N bars" */
{
    cat_u(t, (uint32_t)n);
    cat(t, n == 1 ? " bar" : " bars");
}

/* what is coming, in the horizon's order (the degree first: the FM-1's keys light it); n of them */
static int horizon(hz_t *hz)
{
    int n = 0, i, best = -1;
    int32_t best_bars = 0;
    int d = degree_pending();
    memset(hz, 0, HZ_MAX * sizeof *hz);
    if (rt_loading()) {                  /* the template taking over (pendingTemplateName) */
        cat(cat(hz[n].a, "TEMPLATE "), TPL_NAME[proj.tpl]);
        cat(hz[n].b, "next bar");
        hz[n++].c = K_INFO;
    }
    if (d) {
        cat(hz[n].a, "DEG ");
        cat_u(hz[n].a, (uint32_t)d);
        cat(hz[n].b, "next bar");
        hz[n++].c = K_FOCUS;
    }
    if (ui.st_ok && ui_st.has_arr) {
        cat(hz[n].a, ui_st.composing ? BL_COMP_NAME[ui_st.phase % 7u] : PHASE_NAME[ui_st.phase % 7u]);
        cat(hz[n].b, ui_st.composing ? "next in " : "change in ~");
        bar_count(ui_st.bars_until_change > 0 ? ui_st.bars_until_change : 0, hz[n].b);
        hz[n++].c = K_FOCUS;
    }
    for (i = 0; ui.st_ok && i < ui_st.nup && i < 6; i++) {
        const bl_upcoming_t *u = &ui_st.up[i];
        char *a = hz[n].a;
        switch (u->kind) {
        case BL_UP_KEY: cat(cat(a, "KEY "), NOTE_NAME[u->root % 12]); break;
        case BL_UP_COMPOSER: cat(a, "COMPOSER"); break;
        case BL_UP_RECALL: cat(a, "RECALL"); break;
        case BL_UP_DENSITY: {            /* "DENS" and Swift's description of (value x 100).rounded() / 100 */
            int64_t k = f64_to_i64(f64_round(f64_mul(u->value, F100)));
            uint32_t m = (uint32_t)(k < 0 ? -k : k), f = m % 100u;
            cat_u(cat(a, k < 0 ? "DENS -" : "DENS "), m / 100u);
            if (!f || f % 10u == 0)      /* "0.5", "1.0" */
                cat_u(cat(a, "."), f / 10u);
            else
                cat_u(cat(a, f < 10u ? ".0" : "."), f);
            break;
        }
        case BL_UP_BPART: cat(a, "B-PART"); break;
        case BL_UP_VOICE_ACTIVE:
            caps(a, vid_name(u->voice), 16);
            cat(a, u->on ? " on" : " off");
            break;
        case BL_UP_TENSION: cat(a, "TENSION"); break;
        case BL_UP_ARRANGER: cat(a, u->on ? "AUTO on" : "AUTO off"); break;
        case BL_UP_ADVANCE: cat(a, "ADVANCE"); break;
        case BL_UP_SCENE:
            cat(a, "SCENE ");
            cat(a, u->scene < BL_NSCNS ? BL_SCN_NAME[u->scene] : "");
            break;
        default: break;
        }
        bars_away(u->tick, hz[n].b);
        hz[n++].c = K_INFO;
    }
    for (i = 0; i < nlanes; i++) {       /* the nearest mutation: an active, unlocked voice still repeating */
        const lane_t *l = &lane[i];
        int32_t per = (int32_t)(l->pat->length / BL_TICKS_PER_BAR), bars;
        if (!l->active || l->locked || l->remaining < 0 || l->pat->generator == BL_GEN_ARP ||
            l->pat->generator == BL_GEN_MOTIF)
            continue;
        bars = l->remaining * (per > 1 ? per : 1);
        if (best < 0 || bars < best_bars)
            best = i, best_bars = bars;
    }
    if (best >= 0) {
        cat(cat(hz[n].a, vid_name(lane[best].vid)), " may mutate");
        cat(hz[n].b, "~");
        bar_count(best_bars, hz[n].b);
        hz[n++].c = K_DIM;
    }
    return n;
}

static void draw_perform(void)
{
    hz_t hz[HZ_MAX];
    int32_t rh = nlanes ? LANES_H / nlanes : LANES_H, i, x, y, line;
    int n;
    char t[40];
    if (rh > 20)
        rh = 20;
    for (i = 0; i < nlanes; i++) {
        const lane_t *l = &lane[i];
        const char *m[4];
        uint16_t mc[4], c = vid_color(l->vid);
        int dim = lane_dim(l), nm = marks(l, m, mc), j, ch = rt_channel(l->vid);
        int32_t sh = rh - 2, ty;
        y = 1 + i * rh;
        ty = y + (sh - 12) / 2 + 1;
        cv_round(4, y, 3, sh, 1, rt_muted(l->vid) ? K_DIM2 : c);
        caps(t, vid_name(l->vid), 16);
        cv_text(10, ty, &FONT_T, t, dim ? K_DIM2 : c);
        if (ch >= 0) {
            t[0] = 0;
            cat_u(cat(t, "CH "), (uint32_t)ch + 1u);
            cv_text(62, ty, &FONT_T, t, K_DIM);
        }
        strip(88, y, 105, sh, l, c, dim);
        for (x = 88 + 105 - 1, j = 0; j < nm; j++) {   /* the marks, at the strip's right end */
            if (sh >= 12) {
                x -= text_w(&FONT_T, m[j]) + 6;
                tag(x, y, sh, m[j], mc[j]);
                x -= 2;
            } else {
                x -= 6;
                cv_round(x, y + 1, 4, sh - 2, 1, mc[j]);
            }
        }
        /* Status tags occupy the strip's right edge; the played source must
         * remain visible even when it falls beneath a LOCK/WAIT tag. */
        strip_timing_marker(88, y, 105, sh, l);
        strip_source_marker(88, y, 105, sh, l);
        t[0] = 0;
        cat_u(t, (uint32_t)steps(l->pat));
        text_r(211, ty, &FONT_T, t, K_DIM);
        dots(214, y + (sh - 4) / 2, l);
    }
    /* One shared quarter-note row, below every strip and above HORIZON.
     * Match strip(88, ..., 105, ...)'s fixed 16-step coordinates. */
    if (nlanes)
        for (i = 1; i <= 3; i++)
            cv_rect(88 + i * 105 / 4, LANES_H - 3, 2, 5, mix(K_BG, K_TIMING, 12));
    /* the horizon */
    cv_text(4, 91, &FONT_T, "HORIZON", K_DIM);
    n = horizon(hz);
    for (i = 0, x = 44, line = 0; i < n; i++) {   /* two lines of them; what does not fit is left out */
        int32_t w = chip(0, 0, hz[i].a, hz[i].b, hz[i].c, 0);
        if (x + w > 236) {
            if (line)
                break;
            line = 1;
            x = 4;
        }
        if (x + w > 236)
            continue;
        chip(x, 89 + line * 17, hz[i].a, hz[i].b, hz[i].c, 1);
        x += w + 4;
    }
    if (!n)
        cv_text(44, 90, &FONT_XS, ui.st_ok ? "steady - nothing scheduled" : "stopped", K_DIM);
    /* the convergence, the degree, the tension */
    cv_rect(4, 124, 232, 1, K_LINE);
    cv_text(4, 129, &FONT_T, "LCM", K_DIM);
    convergence_text(convergence(), t, 0);
    cv_text(24, 127, &FONT_XS, t, K_INFO);
    t[0] = 0;
    cat_u(t, (uint32_t)tension_left());
    x = 236 - text_w(&FONT_XS, t);
    cv_text(x, 127, &FONT_XS, t, tension_left() > 0 ? K_TEXT2 : K_DIM2);
    x -= text_w(&FONT_T, "TENSION") + 4;
    cv_text(x, 129, &FONT_T, "TENSION", K_DIM);
    t[0] = 0;
    cat_u(t, (uint32_t)degree_now());
    x -= text_w(&FONT_XS, t) + 10;
    cv_text(x, 127, &FONT_XS, t, degree_now() == 1 ? K_TEXT2 : K_FOCUS);
    x -= text_w(&FONT_T, "DEG") + 4;
    cv_text(x, 129, &FONT_T, "DEG", K_DIM);
}

/* a row of a page: a label (FONT_T, dim) and its value (FONT_XS) */
static void row(int32_t x, int32_t y, int32_t vx, const char *label, const char *val, uint16_t c)
{
    if (ui.view != V_CC && ui.view != V_SETUP) {
        text_box(x, y, vx - x - 8, 14, &FONT_T, label, K_TEXT2, 0);
        text_box(vx, y, 236 - vx, 14, &FONT_XS, val, c, 0);
        return;
    }
    cv_text(x, y + 2, &FONT_T, label, K_DIM);
    cv_text(vx, y, fit(&FONT_XS, val, 236 - vx), val, c);
}

/* the selected voice's name (in its colour) and its marks, at the top of the Voice, Pattern and CC pages */
static void voice_title(const felucca_font_t *f, int32_t y)
{
    const lane_t *l = sel_lane();
    const char *m[4];
    uint16_t mc[4];
    char t[16];
    int vid = key_vid(ui.sel), n = l ? marks(l, m, mc) : 0, j;
    int32_t x = 236;
    int title_h = f->h;
    if (ui.view != V_CC && ui.view != V_SETUP) {
        int available = ui.view == V_VOICE ? 28 : 20;
        if (title_h > available) title_h = available;
    }
    caps(t, vid_name(vid), sizeof t);
    if (ui.view == V_CC || ui.view == V_SETUP)
        cv_text(6, y, f, t, l && lane_dim(l) ? K_DIM2 : vid_color(vid));
    for (j = n - 1; j >= 0; j--) {
        x -= text_w(&FONT_T, m[j]) + 6;
        tag(x, y + (title_h - 14) / 2, 14, m[j], mc[j]);
        x -= 4;
    }
    if (!l) {
        x -= text_w(&FONT_T, "NOT PLAYING") + 6;
        tag(x, y + (title_h - 14) / 2, 14, "NOT PLAYING", K_DIM);
    }
    if (ui.view != V_CC && ui.view != V_SETUP)
        text_box(6, y, x - 12, title_h, f, t, l && lane_dim(l) ? K_DIM2 : vid_color(vid), 0);
}

/* ---- VOICE: the selected voice: its strip, its generation and lifecycle, what waits for its loop, its CC */
static void draw_voice(void)
{
    const lane_t *l = sel_lane();
    const bl_vcfg_t *vc = sel_cfg();
    char t[40];
    voice_title(&FONT_M, 2);
    if (!l || !vc)
        return;
    strip(4, 32, 232, 44, l, vid_color(l->vid), lane_dim(l));
    t[0] = 0;
    cat_u(t, (uint32_t)l->gen);
    row(6, 84, 84, "GENERATION", t, K_TEXT2);
    t[0] = 0;
    if (l->remaining >= 0)
        cat(cat_u(t, (uint32_t)l->remaining), l->remaining == 1 ? " play left" : " plays left");
    else
        cat(t, "established");
    if (l->locked)
        cat(t, ", locked");
    row(6, 99, 84, "LIFECYCLE", t, l->locked ? K_LOCK : K_TEXT2);
    t[0] = 0;
    cat(t, l->pending == BL_REQ_REGENERATE ? "regenerate" : l->pending == BL_REQ_BLANK ? "a blank cycle" : "-");
    row(6, 114, 84, "AT ITS LOOP", t, l->pending ? K_FOCUS : K_TEXT2);
    t[0] = 0;
    if (vc->ncc) {
        caps(t, vc->cc_name[0], 16);
        cat_u(cat(t, " CC "), vc->cc_num[0]);
        if (!vc->nlanes)
            cat(t, ", no lane");
    } else {
        cat(t, "none");
    }
    if (l->pat->generator == BL_GEN_HEARTBEAT && !vc->ncc)
        row(6, 129, 84, "DRUMS", "BD SD CH OH", K_TEXT2);
    else row(6, 129, 84, "CC", t, K_TEXT2);
}

/* ---- PATTERN: the selected voice's pattern in steps, a bar a row: an onset lit (its degree in it where there is
 * room; an accent tangerine), what its gate holds dim, the playhead's step ringed */
static void draw_pattern(void)
{
    const lane_t *l = sel_lane();
    const bl_pattern_t *p;
    int32_t n, rows, rh, i, s;
    uint8_t on[256], acc[256], deg[256], audible[BL_MAX_NOTES];
    uint16_t c;
    char t[24];
    bl_key_t k;
    voice_title(&FONT_B, 2);
    if (!l)
        return;
    p = l->pat;
    if (sel_material()) k = sel_material()->config.key;
    else key_now(&k);
    n = steps(p);
    n = n > 256 ? 256 : n;
    c = vid_color(l->vid);
    memset(on, 0, sizeof on);
    memset(acc, 0, sizeof acc);
    memset(deg, 0, sizeof deg);
    visible_onsets(p, audible);
    for (i = 0; i < p->n; i++) {         /* 1 audible onset, 2 held, 3 suppressed onset */
        int32_t a = (int32_t)(p->note[i].position / STEP_TICKS), b = (int32_t)((p->note[i].position + p->note[i].gate - 1) / STEP_TICKS);
        for (s = a + 1; s <= b && s < n; s++)
            if (!on[s])
                on[s] = 2;
        if (p->generator == BL_GEN_HEARTBEAT && a < n) {
            char label = drum_label(p->note[i].pitch.degree);
            if (!deg[a]) deg[a] = (uint8_t)label;
            else if (deg[a] != (uint8_t)label) deg[a] = '+';
        }
        if (a < n && on[a] != 1 && on[a] != 3) { /* (a chord: its first note's degree) */
            on[a] = audible[i] ? 1 : 3;
            acc[a] = p->note[i].accent;
            if (p->generator != BL_GEN_HEARTBEAT) deg[a] = (uint8_t)degree_label(&p->note[i], &k);
        } else if (a < n && on[a] == 3 && audible[i]) on[a] = 1; /* a retained chord tone sounds here */
    }
    rows = (n + 15) / 16;
    rh = rows ? 116 / rows : 116;
    rh = rh > 16 ? 16 : rh;
    for (s = 0; s < n; s++) {
        int32_t x = 8 + (s % 16) * 14, y = 24 + (s / 16) * rh, h = rh - 2;
        int onset = on[s] == 1 || on[s] == 3;
        uint16_t note_color = lane_dim(l) || l->blank ? mix(K_STEP, c, 8) : c;
        uint16_t f = onset ? (on[s] == 3 ? K_STEP : note_color) : on[s] ? mix(K_STEP, c, 5) : K_STEP;
        cv_round(x, y, 12, h, h >= 8 ? 2 : 0, f);
        if (on[s] == 3) {               /* suppressed: hollow grey cell, never an active-colour fill */
            cv_rect(x, y, 12, 1, K_SUPPRESSED);
            cv_rect(x, y + h - 1, 12, 1, K_SUPPRESSED);
            cv_rect(x, y, 1, h, K_SUPPRESSED);
            cv_rect(x + 11, y, 1, h, K_SUPPRESSED);
        }
        if (on[s] == 1 && acc[s])
            cv_rect(x, y, 12, 2, K_FOCUS);
        if (onset && h >= 13) {
            t[0] = p->generator == BL_GEN_HEARTBEAT ? (char)deg[s] : (char)('0' + deg[s]);
            t[1] = 0;
            text_c(x + 6, y + (h - 12) / 2, &FONT_T, t, on[s] == 3 ? K_SUPPRESSED : K_INVERSE);
        }
        if (s % 4 == 0 && s % 16)        /* the beats */
            cv_rect(x - 2, y + h / 2, 1, 1, K_DIM2);
    }
    if (ui.st_ok && n) {
        s = timing_position(p) / STEP_TICKS;
        if (SHOW_TIMING_PLAYHEAD && l->active && s < n) {
            int32_t x = 8 + (s % 16) * 14, y = 24 + (s / 16) * rh, h = rh - 2;
            cv_rect(x - 1, y - 1, 14, 1, K_TIMING);
            cv_rect(x - 1, y + h, 14, 1, K_TIMING);
            cv_rect(x - 1, y, 1, h, K_TIMING);
            cv_rect(x + 12, y, 1, h, K_TIMING);
        }
        if (l->source_onset) {
            s = (l->source_onset - 1) / STEP_TICKS;
            if (s < n) {
                int32_t x = 8 + (s % 16) * 14, y = 24 + (s / 16) * rh, h = rh - 2;
                cv_rect(x, y, 3, h, K_BG);
                cv_rect(x, y, 2, h, K_SOURCE);
            }
        }
    }
}

/* the selected voice's value in scene j (its first target there), -1 none */
static int scene_value(int j)
{
    const bl_project_t *p = tpl_now();
    int v = key_voice(ui.sel), i;
    for (i = 0; v >= 0 && i < p->scene[j].n; i++)
        if (p->scene[j].voice[i] == v)
            return (int)pct(p->scene[j].value[i]);
    return -1;
}

/* ---- CC: the selected voice's lanes (a cycle of each, center + depth / 2 x sin, its playhead) and the
 * template's scenes (the one the morphs head for lit, the voice's value in each) */
static void draw_cc(void)
{
    const bl_vcfg_t *vc = sel_cfg();
    const bl_project_t *p = tpl_now();
    char t[40];
    int i, j, sc = scene_now();
    int32_t x;
    voice_title(&FONT_B, 2);
    cv_round(4, 22, 232, 58, 3, K_STRIP);
    if (!vc || !vc->nlanes)
        text_c(120, 45, &FONT_T, "NO CC LANE", K_DIM);
    else
        cv_rect(6, 22 + 29, 228, 1, mix(K_STRIP, K_LINE, 10));   /* the middle: 50 % */
    for (i = 0; vc && i < vc->nlanes; i++) {
        const bl_cclane_t *ln = &vc->lane[i];
        int32_t cen = (int32_t)pct(ln->center), dep = (int32_t)pct(ln->depth), ph, py = -1;
        int64_t cyc = f64_to_i64(f64_mul(ln->cycle_bars, F64(0x40AE000000000000)));   /* x 3840.0 */
        uint16_t c = i ? mix(K_STRIP, vid_color(key_vid(ui.sel)), 9) : vid_color(key_vid(ui.sel));
        ph = (int32_t)f64_to_i64(f64_round(f64_mul(ln->phase, F64(0x40B0000000000000))));   /* x 4096.0 */
        for (x = 0; x < 228; x++) {
            int32_t v = cen * 1024 + dep * isin(x * 4096 / 228 + ph) / 2, y;
            v = v < 0 ? 0 : v > 102400 ? 102400 : v;
            y = 22 + 3 + (102400 - v) * 52 / 102400;
            if (py >= 0)
                cv_rect(6 + x, y < py ? y : py, 1, (y < py ? py - y : y - py) + 1, c);
            cv_rect(6 + x, y, 1, 2, c);
            py = y;
        }
        if (ui.st_ok && cyc > 0)
            cv_rect(6 + (int32_t)(rt_play_tick() % cyc) * 228 / (int32_t)cyc, 22, 1, 58, K_FOCUS);
    }
    if (vc && vc->nlanes) {
        const bl_cclane_t *ln = &vc->lane[0];
        t[0] = 0;
        if (ln->target < vc->ncc)
            cat_u(cat(cat(t, vc->cc_name[ln->target]), ", CC "), vc->cc_num[ln->target]);
        cycle_text(ln->cycle_bars, cat(t, ", every "));
        cat(t, f64_eq(ln->cycle_bars, F64(0x3FF0000000000000)) ? " bar" : " bars");
        row(6, 86, 60, "LANE", t, K_TEXT2);
        t[0] = 0;
        cat(cat_u(cat(cat_u(t, pct(ln->depth)), "% around "), pct(ln->center)), "%");
        row(6, 101, 60, "DEPTH", t, K_TEXT2);
    } else if (vc && vc->ncc) {
        t[0] = 0;
        cat_u(cat(cat(t, vc->cc_name[0]), ", CC "), vc->cc_num[0]);
        row(6, 86, 60, "CC MAP", t, K_TEXT2);
    }
    cv_text(6, 122, &FONT_T, "SCENES", K_DIM);
    if (!p->nscenes)
        cv_text(60, 120, &FONT_XS, "none", K_DIM2);
    for (j = 0, x = 60; j < p->nscenes; j++) {
        int v = scene_value(j), on = sc != BL_SCN_NONE && bl_scn(p->scene[j].name) == sc;
        caps(t, p->scene[j].name, 12);
        if (v >= 0) {
            char u[8];
            itoa_u((uint32_t)v, u);
            cat(cat(t, " "), u);
            cat(t, "%");
        }
        x += chip(x, 119, t, 0, on ? K_FOCUS : K_TEXT2, 1) + 4;
    }
}

/* ---- SEQ MOTION: reuse the selected source grid, with motion bindings below. */
static void draw_composer(void)
{
    char t[48];
    int on = ui.st_ok && ui_st.composing;
    int pending = on ? ui_st.pending_section : 0;
    int start = proj_composer_start();
    cv_text(4, 2, &FONT_B, "TANGRAM COMPOSER", K_FOCUS);
    row(4, 24, 64, on ? "CURRENT" : "START", on ? BL_COMP_NAME[ui_st.phase % 7u] :
        BL_COMP_NAME[bl_comp_phase(start)], on ? K_FOCUS : K_DIM);
    t[0] = 0;
    if (pending) {
        cat(t, BL_COMP_NAME[bl_comp_phase(pending - 1)]);
        cat(t, " / NEXT BAR");
    } else if (on) {
        cat_u(t, ui_st.composition_cycle + 1u);
        cat_u(cat(t, "  NEXT IN "), (uint32_t)(ui_st.bars_until_change > 0 ? ui_st.bars_until_change : 0));
        cat(t, ui_st.has_arr ? " BARS" : " PAUSED");
    } else cat(t, proj.composer.enabled ? "ARMED FOR PLAY" : "COMPOSER OFF");
    row(4, 43, 64, pending ? "PENDING" : on ? "CYCLE" : "STATE", t, pending ? K_PENDING : K_TEXT2);
    static const uint8_t phase[] = {BL_PH_BUILD, BL_PH_BED, BL_PH_VARIATION, BL_PH_BPART, BL_PH_SUBTRACT, BL_PH_PAYOFF, BL_PH_ENDING};
    static const char *const label[] = {"IN", "A", "DEV", "B", "BRK", "A'", "OUT"};
    for (int i = 0; i < 7; i++) {
        int active = on && ui_st.phase == phase[i];
        cv_round(5 + i * 33, 65, 30, 21, 2, active ? K_FOCUS : K_STEP);
        text_c(20 + i * 33, 69, &FONT_T, label[i], active ? K_INVERSE : K_DIM);
        /* Match ALGORITHM's target: pending/current while playing, START
         * while stopped. Fill still identifies the current sounding section. */
        if (proj.composer.enabled && (pending ? pending == i + 1 : on ? active : start == i)) {
            if (active) cv_rect(5 + i * 33, 82, 30, 2, K_BG);
            cv_rect(7 + i * 33, 84, 26, 2, K_FOCUS);
        }
    }
    rt_counters_t counters = rt_counters();
    text_box(4, 94, 72, 12, &FONT_T, "TIME", K_DIM, 0);
    text_box(84, 94, 72, 12, &FONT_T, "PART", K_DIM, 0);
    text_box(164, 94, 72, 12, &FONT_T, "TOTAL", K_DIM, 0);
    t[0] = 0;
    uint32_t minutes = counters.seconds / 60u, seconds = counters.seconds % 60u;
    if (minutes < 10) cat(t, "0");
    cat_u(t, minutes);
    cat(t, seconds < 10 ? ":0" : ":");
    cat_u(t, seconds);
    text_box(4, 107, 72, 18, &FONT_XS, t, K_TEXT2, 0);
    t[0] = 0;
    cat_u(t, counters.part_bars);
    text_box(84, 107, 72, 18, &FONT_XS, t, K_TEXT2, 0);
    t[0] = 0;
    cat_u(t, counters.total_bars);
    text_box(164, 107, 72, 18, &FONT_XS, t, K_TEXT2, 0);
    if (on && (ui_st.motion_pending || ui_st.motion_active)) {
        t[0] = 0;
        cat_u(cat(t, "AUTO FRAMES "), ui_st.motion_active);
        cat_u(cat(t, " / NEXT LOOP "), ui_st.motion_pending);
        cv_text(4, 130, &FONT_T, t, K_INFO);
    } else cv_text(4, 130, &FONT_T, proj.composer.enabled ? "ALGORITHM: STAGE / NEXT BAR" : "ENABLE COMPOSE TO SELECT", K_DIM);
}

static void draw_arranger(void)
{
    draw_pattern();                     /* MOTION reuses the existing selected-source grid */
}

/* ---- MIDI DELAY: actual per-voice output settings, not the legacy audio-delay hint. */
static void draw_delay(void)
{
    const bl_project_t *p = tpl_now();
    const felucca_font_t *f = p->nvoices > 6 ? &FONT_T : &FONT_XS;
    int32_t rh = p->nvoices > 6 ? 12 : 14, i, y;
    char t[40];
    text_box(4, 0, 232, 22, &FONT_B, "MIDI DELAY", K_FOCUS, 0);
    t[0] = 0;
    cat_u(cat(t, "per voice @ "), (uint32_t)bpm_now());
    cat(t, " BPM");
    text_box(4, 24, 232, 12, &FONT_T, t, K_TEXT2, 0);
    text_box(4, 42, 84, 12, &FONT_T, "VOICE", K_TEXT2, 0);
    text_box(96, 42, 44, 12, &FONT_T, "STATE", K_TEXT2, 0);
    text_box(152, 42, 84, 12, &FONT_T, "TIME", K_TEXT2, 2);
    cv_rect(4, 55, 232, 1, K_LINE);
    for (i = 0, y = 58; i < p->nvoices; i++, y += rh) {
        const bl_voice_t *v = voice_material(bl_vid(p->voice[i].id));
        bl_delay_t d = v ? v->delay : proj.delay[i];
        text_box(4, y, 84, rh, f, vid_name(bl_vid(p->voice[i].id)), vid_color(bl_vid(p->voice[i].id)), 0);
        text_box(96, y, 44, rh, f, d.enabled ? "ON" : "OFF", d.enabled ? K_INFO : K_TEXT2, 0);
        text_box(152, y, 84, rh, f, TIME_LABEL[d.time - 1], K_TEXT, 2);
    }
}

static void seed_hex(char *t, int groups)   /* the seed as 16 hex digits (groups: in fours, spaced) */
{
    int i, k = 0;
    for (i = 0; i < 16; i++) {
        uint32_t d = (uint32_t)(proj.seed >> (60 - 4 * i)) & 15u;
        if (groups && i && i % 4 == 0)
            t[k++] = ' ';
        t[k++] = (char)(d < 10u ? '0' + d : 'A' + d - 10u);
    }
    t[k] = 0;
}

/* GLO performance: the bank's selection and a separate, uncommitted name buffer. */
static void draw_performance_presets(void)
{
    char slot[4] = {'0', (char)('1' + ui.perf_slot), 0}, name[PERFORMANCE_PRESET_NAME_BYTES + 1];
    int used = performance_preset_used(ui.perf_slot + 1), editing = ui.perf_phase == PERF_EDIT;
    if (ui.perf_slot == 9) { slot[0] = '1'; slot[1] = '0'; }
    row(4, 3, 64, "SLOT", slot, K_FOCUS);
    row(4, 23, 64, "ACTION", PERF_OP_NAME[ui.perf_op], K_TEXT);
    performance_name(name);
    if (editing) {
        for (int i = 0; i < PERFORMANCE_PRESET_NAME_BYTES; i++) {
            char ch[2] = {name[i] == ' ' ? '_' : name[i], 0};
            text_box(10 + i * 18, 48, 17, 22, &FONT_B, ch, i == ui.perf_pos ? K_FOCUS : K_TEXT, 1);
        }
        cv_rect(10 + ui.perf_pos * 18, 73, ui.perf_pos == PERFORMANCE_PRESET_NAME_BYTES ? 8 : 17, 2, K_FOCUS);
    } else {
        text_box(6, 48, 228, 26, &FONT_B, used < 0 ? "ERROR" : used ? name : "EMPTY", K_TEXT, 0);
    }
    text_box(6, 85, 228, 20, &FONT_XS, performance_status(),
        ui.perf_phase == PERF_OVERWRITE || ui.perf_feedback == PERF_ERROR ? K_MUTED : K_FOCUS, 0);
    cv_text(6, 110, &FONT_T, editing ? "K3 POSITION; K4 CHARACTER" : "K1 SLOT; K2 ACTION", K_TEXT2);
    cv_text(6, 128, &FONT_T, editing ? ui.perf_char == PERF_BACKSPACE ? "K3 LEAVES BS; K1 CANCELS" :
        "_ SPACE; K1 / GLO CANCEL" : ui.perf_phase == PERF_BROWSE ? "SEL PREPARE; GLO: SETUP" :
        "SEL CONFIRM; GLO: SETUP", K_DIM);
}

/* ---- SETUP: the template, the key, the clock, MIDI out, the version; the seed as 16 hex digits (a run is
 * reproducible on the Mac with it) */
static void draw_setup(void)
{
    char t[40];
    int32_t x, i;
    if (ui.page[V_SETUP] == 3) {
        draw_performance_presets();
        return;
    }
    if (ui.page[V_SETUP] == 1) {
        device_settings_t settings = screen_clock();
        int status = device_settings_status();
        text_box(4, 3, 232, 22, &FONT_B, "GLOBAL MIDI", K_FOCUS, 0);
        row(4, 31, 92, "CLOCK OUT", DEVICE_MIDI_NAME[settings.clock_out - DEVICE_MIDI_USB], K_TEXT);
        row(4, 52, 92, "MIDI OUT", DEVICE_MIDI_NAME[settings.midi_out - DEVICE_MIDI_USB], K_TEXT);
        row(4, 73, 92, "BLE LINK", plat_ble_connected() ? "CONNECTED" : "NOT CONNECTED", K_TEXT2);
        row(4, 94, 92, "GLOBAL SAVE", status < 0 ? "ERROR" : status ? "PENDING" : "SAVED", K_TEXT2);
        cv_text(6, 125, &FONT_T, "K1 CLOCK OUT; K2 MIDI OUT", K_DIM);
        cv_text(6, 139, &FONT_T, "USB / BLE / USB + BLE", K_DIM);
        return;
    }
    if (ui.page[V_SETUP] == 2) {
        int status = device_settings_status();
        text_box(4, 3, 232, 22, &FONT_B, "GLOBAL APPEARANCE", K_FOCUS, 0);
        row(4, 28, 64, "THEME", DEVICE_THEME_NAME[device_settings_get().theme], K_FOCUS);
        row(4, 47, 64, "SAVE", status < 0 ? "ERROR / K2 RETRY" :
            status ? rt_playing() ? "PENDING / STOP" : "WAITING" : "SAVED", status < 0 ? K_MUTED : K_TEXT2);
        for (i = 0; i < BL_NVIDS; i++) cv_rect(6 + i * 33, 72, 29, 6, vid_color(i));
        cv_text(6, 86, &FONT_T, "ACTIVE", K_TEXT);
        cv_rect(60, 87, 26, 5, vid_color(BL_VID_BED1));
        cv_text(100, 86, &FONT_T, "SUPPRESSED", K_SUPPRESSED);
        for (i = 0; i < 4; i++) cv_rect(174 + i * 7, 89, 4, 1, K_SUPPRESSED);
        cv_rect(6, 107, 2, 12, K_SOURCE);
        cv_text(14, 107, &FONT_T, "SOURCE", K_TEXT2);
        cv_rect(100, 107, 1, 12, K_TIMING);
        cv_text(108, 107, &FONT_T, "TIME", K_TEXT2);
        cv_text(6, 130, &FONT_T, "K1 THEME; GLO: PERFORMANCE", K_DIM);
        return;
    }
    row(4, 4, 76, "TEMPLATE", TPL_TITLE[proj.tpl], K_TEXT);
    t[0] = 0;
    cat(cat(cat(t, NOTE_NAME[proj.key_root]), " "), MODE_NAME[proj.key_mode]);
    row(4, 20, 76, "KEY", t, K_TEXT);
    t[0] = 0;
    if (screen_clock().sync)
        cat(cat(t, "EXT from "), GLO_EXT_NAMES[port_index()]);
    else
        cat(cat(t, "INT via "), DEVICE_MIDI_NAME[screen_clock().clock_out - DEVICE_MIDI_USB]);
    row(4, 36, 76, "CLOCK", t, K_TEXT);
    row(4, 52, 76, "MIDI OUT", DEVICE_MIDI_NAME[screen_clock().midi_out - DEVICE_MIDI_USB], K_TEXT);
    row(4, 68, 76, "VERSION", BL_VERSION, K_TEXT2);
    cv_rect(4, 88, 232, 1, K_LINE);
    cv_text(4, 94, &FONT_T, "SEED", K_DIM);
    seed_hex(t, 0);
    for (i = 0, x = 120 - (text_w(&FONT_B, t) + 18) / 2; i < 4; i++) {
        char g[5];
        memcpy(g, t + 4 * i, 4);
        g[4] = 0;
        x = cv_text(x, 108, &FONT_B, g, proj_seeded() ? K_TEXT : K_DIM) + 6;
    }
    if (!proj_seeded())
        text_c(120, 126, &FONT_XS, "a new one at the first Play", K_DIM);
}

static void draw_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    char title[24];
    title[0] = 0;
    cat(cat(title, BTN_LABEL[ui.help - 1]), " ");
    cat_u(title, (uint32_t)ui.help_part + 1u);
    cat_u(cat(title, "/"), (uint32_t)help_count(ui.help - 1));
    cv_round(16, 12, 208, 124, 6, K_PANEL);   /* (four lines at most) */
    cv_round(16, 12, 208, 26, 6, K_BG);
    cv_rect(16, 37, 208, 1, K_LINE);
    text_box(22, 15, 196, 22, &FONT_B, title, K_FOCUS, 1);
    text_c(120, 38, &FONT_T, help_count(ui.help - 1) > 1 ? "KEEP HOLDING: MORE HELP" : "RELEASE TO CLOSE", K_DIM);
    for (k = 0; k < n; k++)
        text_box(22, 50 + k * 20, 196, 18, &FONT_S, l[k] + (l[k][0] == ' '), K_TEXT, 1);
}

/* what the page shows, as a signature: the band is drawn again when it changes (the playheads move it by the step) */
static uint32_t main_sig(void)
{
    uint32_t h = hash(hash(2166136261u, ui.view | (uint32_t)ui.sel << 4 | (uint32_t)ui.st_ok << 8 | proj.tpl << 9),
                      (uint32_t)bpm_now() | (uint32_t)proj.key_root << 16 | (uint32_t)proj.key_mode << 20);
    const uint8_t *b;
    uint32_t i, n;
    int k;
    if (ui.help)
        return hash(hash(hash(0x48454C50u, ui.help), ui.help_part), device_settings_get().theme);
    h = hash(h, device_settings_get().theme);
    if (ui.view == V_SETUP) h = hash(h, (uint32_t)device_settings_status());
    if (performance_page()) {
        char name[PERFORMANCE_PRESET_NAME_BYTES + 1];
        performance_name(name);
        h = hash_s(hash_s(h, name), performance_status());
        h = hash(h, ui.perf_phase | (uint32_t)ui.perf_pos << 8 | (uint32_t)ui.perf_char << 16);
    }
    h = hash(h, ui.page[ui.view]);
    if (ui.view == V_PERFORM && ui.page[V_PERFORM] == 2) {
        h = hash(h, (uint32_t)proj_composer_start()); /* stopped ALGORITHM changes no voice/cell state */
        rt_counters_t counters = rt_counters();
        h = hash(hash(hash(h, counters.seconds), counters.part_bars), counters.total_bars);
    }
    for (int j = 0; j < 4; j++) {
        cell_t c;
        cell(ui.view, j, &c);
        h = hash_s(hash_s(h, c.val), c.sub);
    }
    h = hash(hash(h, ui.pre_sig), (uint32_t)proj.arr_has | proj.arr_on << 1 | proj.arr_ambient << 2 |
                                      proj.arr_reprise << 3 | (uint32_t)proj.arr_min << 8 | (uint32_t)proj.arr_max << 20);
    h = hash(h, (uint32_t)screen_clock().midi_out | (uint32_t)screen_clock().clock_out << 8);
    h = hash(hash(h, screen_clock().sync | screen_clock().ext_ports << 1 | (uint32_t)proj.density_pct << 8 | (uint32_t)proj_seeded() << 16 |
                         (uint32_t)rt_loading() << 17),
             (uint32_t)proj.seed ^ (uint32_t)(proj.seed >> 32));
    for (k = 0; k < BL_NVIDS; k++)
        h = hash(h, (uint32_t)rt_muted(k) | (uint32_t)(rt_channel(k) + 1) << 1);
    for (k = 0; k < BL_MAX_VOICES; k++)
        h = hash(h, proj.channel[k]);
    if (ui.st_ok) {                      /* the snapshot, and where the playheads are (by the 16th) */
        b = (const uint8_t *)&ui_st;
        for (i = 0, n = sizeof ui_st; i < n; i++)
            h = hash(h, b[i]);
        if (ui.view == V_PERFORM || ui.view == V_VOICE || ui.view == V_PATTERN || ui.view == V_CC)
            h = hash(h, (uint32_t)(rt_play_tick() / STEP_TICKS));
        if (SHOW_TIMING_PLAYHEAD && (ui.view == V_PERFORM || ui.view == V_VOICE ||
            ui.view == V_PATTERN || ui.view == V_ARRANGER))
            h = hash(h, (uint32_t)rt_play_tick()); /* redraw between onsets, including Density 0 */
    }
    for (k = 0; k < nlanes; k++) {       /* the patterns: a mutation changes them */
        const bl_pattern_t *p = lane[k].pat;
        h = hash(h, lane[k].source_onset); /* gate endings/rests redraw without waiting for a timing step */
        h = hash(h, (uint32_t)p->length);
        if (p->generator == BL_GEN_ARP || p->generator == BL_GEN_MOTIF)
            h = hash(h, p->groove);      /* active groove can change without changing source positions */
        h = hash(hash(h, p->n | (uint32_t)p->locked << 8), (uint32_t)p->seed);
        for (i = 0; i < p->n; i++)
            h = hash(h, (uint32_t)p->note[i].position ^ (uint32_t)p->note[i].pitch.degree << 20);
    }
    return h;
}

static void draw_main(void)
{
    uint32_t h;
    if (ui.frame % 3u)                   /* 20 frames a second is plenty */
        return;
    h = main_sig();
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    if (ui.help)
        draw_help();
    else
        switch (ui.view) {
        case V_PERFORM: if (ui.page[V_PERFORM] == 2) draw_composer(); else draw_perform(); break;
        case V_VOICE: draw_voice(); break;
        case V_PATTERN: draw_pattern(); break;
        case V_CC: draw_cc(); break;
        case V_ARRANGER: draw_arranger(); break;
        case V_DELAY: draw_delay(); break;
        default: draw_setup(); break;
        }
    cv_blit(0, MAIN_Y);
}

/* Tangram's four 60px columns keep labels, indicators and values in separate
 * bands. Large values must fit the remaining height, not just column width. */
static const char *compact_knob_label(int k, int i)
{
    switch (k) {
    case KN_PERF_SLOT: return "SLOT";
    case KN_PERF_OP: return "ACTION";
    case KN_PERF_POS: return "POS";
    case KN_PERF_CHAR: return "CHAR";
    case KN_THEME: return "THEME";
    case KN_DEVICE_RETRY: return "SAVE";
    case KN_COMPOSER: return "COMPOSE";
    case KN_COMP_BARS: return "BARS";
    case KN_COMP_EVOLUTION: return "EVOLVE";
    case KN_COMP_TENSION: return "TENSION";
    case KN_VDENSITY: return "DENSITY";
    case KN_GENERATOR: return "GEN";
    case KN_DIRECTION: return "DIR";
    case KN_LOOP: return "STEPS";
    case KN_REGISTER: return "REG";
    case KN_CHANNEL: return "CH";
    case KN_FEEDBACK: return "FDBK";
    case KN_DELAY_TIME: return "TIME";
    case KN_NONE: return "-";
    default: return knob_label(ui.view, i);
    }
}

static void draw_parameter_cell(int i, const cell_t *c, int hot)
{
    int k = view_knob(ui.view, i);
    int32_t x = i * 60, cx = x + 30;
    uint16_t bg = hot ? K_PANEL : K_BG;
    if (i) cv_rect(x, 14, 1, 52, K_LINE);
    if (hot) cv_round(x + 3, 14, 54, 52, 5, K_PANEL);
    char number[3] = {'K', (char)('1' + i), 0}, label[16];
    caps(label, compact_knob_label(k, i), sizeof label);
    text_box(x + 4, 14, 14, 12, &FONT_T, number, K_FOCUS, 0);
    text_box(x + 21, 14, 36, 12, &FONT_T, label, k == KN_NONE ? K_DIM : hot ? K_FOCUS : K_TEXT2, 0);
    if (c->kind == CELL_TEXT) {
        int has_sub = c->sub[0] != 0;
        text_box(x + 3, has_sub ? 30 : 46, 54, 20, &FONT_B, c->val,
                 k == KN_NONE ? K_DIM : K_TEXT2, 1);
        if (has_sub) text_box(x + 3, 52, 54, 12, &FONT_T, c->sub, K_TEXT2, 1);
        return;
    }
    if (c->kind == CELL_PIPS) {
        int n = c->hi - c->lo + 1, pw = n > 4 ? 6 : 8, gap = 3;
        int width = n * (pw + gap) - gap;
        for (int j = 0; j < n; j++)
            cv_round(cx - width / 2 + j * (pw + gap), 33, pw, 6, 2,
                     j == c->v - c->lo ? K_FOCUS : K_LINE);
    } else {
        int fraction = c->hi > c->lo ? (clamp(c->v, c->lo, c->hi) - c->lo) * 1024 / (c->hi - c->lo) : 0;
        knob_ring(cx, 35, 8, fraction, K_LINE, K_FOCUS, bg);
    }
    text_box(x + 3, 46, 54, 20, &FONT_B, c->val, hot ? K_FOCUS : K_TEXT, 1);
}

/* the knob strip (AMB-1's): a rule with the page's name and seven page dots at its right end; under it a cell a
 * knob: its name, a ring (or pips, for a switch of a few positions) and the value where the knob changes it, the
 * value alone (and a line under it) where it is shown only. The knob being turned is drawn on a tint */
static void draw_knobs(void)
{
    cell_t c[4];
    int tangram = ui.view != V_CC && ui.view != V_SETUP;
    int now_touch = plat_ms() < ui.touch_until, i;
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)(ui.touched + 1) << 8 | (uint32_t)now_touch << 16);
    h = hash(h, ui.page[ui.view]);
    for (i = 0; i < 4; i++) {
        cell(ui.view, i, &c[i]);
        h = hash_s(hash_s(hash(h, (uint32_t)c[i].v), c[i].val), c[i].sub);
    }
    h = hash(h, device_settings_get().theme);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    {   /* the rule; at its right end, where you are: the page, then the dots */
        char t[24];
        int k;
        int32_t xd = 237 - NVIEWS * 6, xt;
        caps(t, view_name(ui.view), sizeof t);
        if (tangram) {
            text_box(4, 0, 150, 12, &FONT_XS, t, K_FOCUS, 0);
            if (VIEW_PAGES[ui.view] > 1) {
                char page[4] = {(char)('1' + ui.page[ui.view]), '/', (char)('0' + VIEW_PAGES[ui.view]), 0};
                text_box(162, 0, 24, 12, &FONT_T, page, K_TEXT2, 2);
            }
        } else {
            xt = xd - 5 - text_w(&FONT_XS, t);
            cv_rect(4, 5, xt - 9, 1, K_LINE);
            cv_text(xt, -1, &FONT_XS, t, K_FOCUS);
        }
        for (k = 0; k < NVIEWS; k++)
            cv_round(xd + k * 6, 3, 4, 4, 2, k == ui.view ? K_FOCUS : K_LINE);
    }
    for (i = 0; i < 4; i++) {
        int k = view_knob(ui.view, i), hot = now_touch && ui.touched == i && k != KN_NONE && k < KN_SHOWN;
        if (tangram) {
            draw_parameter_cell(i, &c[i], hot);
            continue;
        }
        int32_t x = i * 60, cx = x + 30;
        char t[16];
        const felucca_font_t *f;
        uint16_t bg = hot ? K_PANEL : K_BG;
        if (i)
            cv_rect(x, 16, 1, 46, K_LINE);
        if (hot)
            cv_round(x + 3, 11, 54, 57, 6, K_PANEL);
        caps(t, knob_label(ui.view, i), sizeof t);
        f = fit(&FONT_XS, t, 56);
        text_c(cx, f == &FONT_T ? 14 : 12, f, t, hot ? K_TEXT : K_DIM);
        if (c[i].kind == CELL_TEXT) {    /* shown only */
            f = fit(&FONT_B, c[i].val, 56);
            text_c(cx, f == &FONT_T ? 31 : 29, f, c[i].val, K_TEXT2);
            text_c(cx, 48, fit(&FONT_T, c[i].sub, 56), c[i].sub, K_DIM);
            continue;
        }
        if (c[i].kind == CELL_PIPS) {    /* a switch: its positions as pips */
            int n = c[i].hi - c[i].lo + 1, j, pw = n > 4 ? 6 : 9, gap = 3, w0 = n * (pw + gap) - gap;
            for (j = 0; j < n; j++)
                cv_round(cx - w0 / 2 + j * (pw + gap), 33, pw, 8, 2, j == c[i].v - c[i].lo ? K_FOCUS : K_LINE);
        } else {
            knob_ring(cx, 37, 12, c[i].v < c[i].lo ? 0 : (c[i].v - c[i].lo) * 1024 / (c[i].hi - c[i].lo), K_LINE, K_FOCUS, bg);
        }
        f = fit(&FONT_B, c[i].val, 56);
        text_c(cx, f == &FONT_T ? 52 : 50, f, c[i].val, hot ? K_FOCUS : K_TEXT);
    }
    cv_blit(0, KNB_Y);
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader on the computer (announce.c; fm1-reader), AMB-1's way: a page once it
 * settles, with its four values; a value the knob changes as it turns; a help card as it comes up; what a button did
 * (say); a transport started or stopped from MIDI or by the arranger's ending; the arranger's phase as it moves
 * (queued). WHERE (the reader asks, or it has just started listening) is the whole screen: the page, its values and
 * the header, then what the page shows. Nothing is built while no reader listens. */
static struct {
    uint8_t view, page, help, help_part, started, playing, phase;
    uint32_t val[4];                     /* the cells' values, hashed */
} sp;
static char sp_buf[ANN_MAX + 1];
static uint32_t sp_n;
static void sp_say_queued(const char *t) { ann_say(t, 0, 0); }
static void (*sp_out)(const char *t) = sp_say_queued;   /* where a page's lines go (speak_dump: printed) */

static void sp_clear(void)
{
    sp_n = 0;
    sp_buf[0] = 0;
}
static void sp_cat(const char *s)
{
    while (*s && sp_n < ANN_MAX)
        sp_buf[sp_n++] = *s++;
    sp_buf[sp_n] = 0;
}
static void sp_int(uint32_t v)
{
    char t[12];
    itoa_u(v, t);
    sp_cat(t);
}
static void sp_flush(void)
{
    if (sp_n)
        sp_out(sp_buf);
    sp_clear();
}
static void sp_item(const char *s, const char *sep)   /* s after what is gathered (sep between); said first when
                                                       * both would not fit in one SAY */
{
    uint32_t k = 0;
    while (s[k])
        k++;
    if (sp_n && sp_n + 2u + k > ANN_MAX)
        sp_flush();
    if (sp_n)
        sp_cat(sep);
    sp_cat(s);
}

/* a cell as it is said: its name, its value, and the line under it where it says what the value is ("Loop 16
 * STEPS"); a range as "G2 to G4", "8 to 16" */
static void sp_knob(int view, int i)
{
    static const uint8_t WITH_SUB[] = {KN_PERF_SLOT, KN_LOOP, KN_DRUM_PARTS, KN_DRUM_GROOVE,
        KNS_LOOP, KNS_GEN, KNS_TARGET, KNS_CYCLE, KNS_LENGTH, KNS_PHASE, KNS_PLAYS};
    cell_t c;
    uint32_t j;
    int k = view_knob(view, i);
    cell(view, i, &c);
    if (view != V_CC && view != V_SETUP) {
        char number[4] = {'K', (char)('1' + i), ' ', 0};
        sp_cat(number);
    }
    sp_cat(knob_label(view, i));
    sp_cat(" ");
    if (k == KN_PERF_CHAR && ui.perf_phase == PERF_EDIT) {
        if (ui.perf_char == PERF_BACKSPACE) sp_cat("Backspace. SEL deletes once to the left. Move K3 to leave backspace and save.");
        else if (PERF_CHARS[ui.perf_char] == ' ') sp_cat("Space");
        else { char ch[2] = {PERF_CHARS[ui.perf_char], 0}; sp_cat(ch); }
        return;
    }
    if (k == KNS_REGISTER && sel_cfg()) {   /* (its notes may have a minus of their own) */
        char t[24];
        reg_text(sel_cfg()->reg_low, sel_cfg()->reg_high, t, 1);
        sp_cat(t);
    }
    if (k == KN_REGISTER && sel_material()) {
        char t[24];
        const bl_vconfig_t *cfg = &sel_material()->pending;
        reg_text(cfg->reg_low, cfg->reg_high, t, 1);
        sp_cat(t);
        return;
    }
    if ((k == KN_GENERATOR || k == KN_SCALE) && sel_material()) {
        const bl_vconfig_t *cfg = &sel_material()->pending;
        sp_cat(k == KN_GENERATOR ? GEN_NAME[cfg->gen.kind] : MODE_NAME[cfg->key.scale.mode]);
        return;
    }
    for (j = 0; !(k == KNS_REGISTER && sel_cfg()) && c.val[j]; j++) {
        char ch[2] = {c.val[j], 0};
        if (k == KNS_LENGTH && c.val[j] == '-')
            sp_cat(" to ");
        else
            sp_cat(ch);
    }
    for (j = 0; j < sizeof WITH_SUB; j++)
        if (WITH_SUB[j] == k && c.sub[0]) {
            sp_cat(" ");
            sp_cat(c.sub);
        }
}

/* the page: its name (a voice's page: the voice's too, as its title shows it) and its values; all: the header too
 * (the template, its key, the tempo, playing or stopped) */
static void sp_page(int all)
{
    int i;
    sp_clear();
    sp_cat(view_name(ui.view));
    sp_cat(" page");
    if ((ui.view == V_VOICE || ui.view == V_PATTERN || ui.view == V_CC) && key_voice(ui.sel) >= 0) {
        sp_cat(", ");
        sp_cat(vid_name(key_vid(ui.sel)));
    }
    for (i = 0; i < 4; i++) {
        sp_cat(i ? ", " : ": ");
        sp_knob(ui.view, i);
    }
    if (!all)
        return;
    sp_cat(". ");
    sp_cat(TPL_TITLE[proj.tpl]);
    sp_cat(", ");
    sp_cat(NOTE_NAME[proj.key_root]);
    sp_cat(" ");
    sp_cat(MODE_NAME[proj.key_mode]);
    sp_cat(", tempo ");
    if (screen_clock().sync)
        sp_cat("EXT");
    else
        sp_int((uint32_t)bpm_now());
    sp_cat(rt_playing() ? ", playing" : ", stopped");
    if (proj_save_pending())
        sp_cat(", saving at the stop");
}

static void sp_lane(const lane_t *l, char *t)   /* "Pluck 1, channel 1, 16 steps, 4 plays left, muted" */
{
    int ch = rt_channel(l->vid);
    t[0] = 0;
    cat(t, vid_name(l->vid));
    if (ch >= 0)
        cat_u(cat(t, ", channel "), (uint32_t)ch + 1u);
    cat(cat_u(cat(t, ", "), (uint32_t)steps(l->pat)), " steps, ");
    if (l->remaining >= 0)
        cat(cat_u(t, (uint32_t)l->remaining), l->remaining == 1 ? " play left" : " plays left");
    else
        cat(t, "established");
    if (!l->active)
        cat(t, ", waits");
    if (rt_muted(l->vid))
        cat(t, ", muted");
    if (l->locked)
        cat(t, ", locked");
    if (l->blank)
        cat(t, ", blank");
}

/* what the page shows, in as many SAYs as it takes (sp_out) */
static void sp_main(void)
{
    char t[96];
    int i;
    sp_clear();
    switch (ui.view) {
    case V_PERFORM: {
        hz_t hz[HZ_MAX];
        int n;
        if (ui.page[V_PERFORM] == 2) {
            sp_cat("Tangram Composer ");
            if (ui.st_ok && ui_st.composing) {
                sp_cat(BL_COMP_NAME[ui_st.phase % 7u]);
                if (!ui_st.has_arr) sp_cat(", paused");
                sp_cat(", cycle "); sp_int(ui_st.composition_cycle + 1u);
                sp_cat(", next section in ");
                sp_int((uint32_t)(ui_st.bars_until_change > 0 ? ui_st.bars_until_change : 0));
                sp_cat(" bars");
            } else sp_cat(proj.composer.enabled ? "armed or paused" : "off");
            sp_flush();
            if (ui.st_ok && ui_st.composing && ui_st.pending_section) {
                sp_cat("Pending ");
                sp_cat(BL_COMP_NAME[bl_comp_phase(ui_st.pending_section - 1)]);
                sp_cat(" at the next bar.");
            } else if (!rt_playing()) {
                sp_cat("Starting section ");
                sp_cat(BL_COMP_NAME[bl_comp_phase(proj_composer_start())]);
            }
            sp_flush();
            rt_counters_t counters = rt_counters();
            sp_cat("Playback time "); sp_int(counters.seconds / 60u);
            sp_cat(" minutes "); sp_int(counters.seconds % 60u); sp_cat(" seconds.");
            sp_flush();
            sp_cat("Part "); sp_int(counters.part_bars);
            sp_cat(" bars. Total "); sp_int(counters.total_bars); sp_cat(" bars.");
            sp_flush();
            sp_cat("Master times voice density times section activity.");
            sp_flush();
            sp_cat("Manual voice edits take ownership until Stop.");
            break;
        }
        for (i = 0; i < nlanes; i++) {
            sp_lane(&lane[i], t);
            sp_item(t, "; ");
        }
        sp_flush();
        n = horizon(hz);
        sp_cat("Horizon:");
        if (!n)
            sp_cat(ui.st_ok ? " steady, nothing scheduled" : " stopped");
        for (i = 0; i < n; i++) {        /* "BED, change in about 4 bars" */
            char *e, *b = hz[i].b;
            t[0] = 0;
            e = cat(cat(t, hz[i].a), ", ");
            for (; *b; b++)
                e = *b == '~' ? cat(e, "about ") : (e[0] = *b, e[1] = 0, e + 1);
            sp_item(t, i ? "; " : " ");
        }
        sp_flush();
        sp_cat("Convergence ");
        convergence_text(convergence(), t, 1);
        sp_cat(t);
        sp_cat(". Tension ");
        sp_int((uint32_t)tension_left());
        sp_cat(". Degree ");
        sp_int((uint32_t)degree_now());
        break;
    }
    case V_VOICE:
    case V_PATTERN:
    case V_ARRANGER: {
        const lane_t *l = sel_lane();
        if (!l) {
            sp_cat(vid_name(key_vid(ui.sel)));
            sp_cat(", not playing");
            break;
        }
        sp_lane(l, t);
        sp_cat(t);
        sp_cat(", generation ");
        sp_int((uint32_t)l->gen);
        if (l->pending)
            sp_cat(l->pending == BL_REQ_REGENERATE ? ", regenerates at its loop" : ", a blank cycle at its loop");
        if ((ui.view == V_PATTERN || ui.view == V_ARRANGER) && l->pat->n) { /* source-grid onsets */
            int32_t n = steps(l->pat), last = -1;
            sp_flush();
            sp_int(l->pat->n);
            sp_cat(l->pat->n == 1 ? " note" : " notes");
            for (i = 0; n <= 32 && i < l->pat->n; i++) {
                int32_t s = (int32_t)(l->pat->note[i].position / STEP_TICKS) + 1;
                if (s == last)
                    continue;
                sp_cat(last < 0 ? ", on steps " : ", ");
                sp_int((uint32_t)s);
                last = s;
            }
        }
        break;
    }
    case V_CC: {
        const bl_vcfg_t *vc = sel_cfg();
        const bl_project_t *p = tpl_now();
        sp_cat(vid_name(key_vid(ui.sel)));
        if (vc && vc->nlanes) {
            const bl_cclane_t *ln = &vc->lane[0];
            sp_cat(", lane ");
            if (ln->target < vc->ncc) {
                sp_cat(vc->cc_name[ln->target]);
                sp_cat(" CC ");
                sp_int(vc->cc_num[ln->target]);
            }
            sp_cat(", every ");
            cycle_text(ln->cycle_bars, t);
            sp_cat(t);
            sp_cat(f64_eq(ln->cycle_bars, F64(0x3FF0000000000000)) ? " bar, depth " : " bars, depth ");
            sp_int(pct(ln->depth));
            sp_cat("% around ");
            sp_int(pct(ln->center));
            sp_cat("%");
        } else {
            sp_cat(", no CC lane");
        }
        sp_cat(". Scenes");
        if (!p->nscenes)
            sp_cat(": none");
        for (i = 0; i < p->nscenes; i++) {
            int v = scene_value(i);
            sp_cat(i ? ", " : ": ");
            sp_cat(p->scene[i].name);
            if (v >= 0) {
                sp_cat(" ");
                sp_int((uint32_t)v);
                sp_cat("%");
            }
            if (scene_now() != BL_SCN_NONE && bl_scn(p->scene[i].name) == scene_now())
                sp_cat(", the one now");
        }
        break;
    }
    case V_DELAY: {
        const bl_project_t *p = tpl_now();
        for (i = 0; i < p->nvoices; i++) {
            int vid = bl_vid(p->voice[i].id);
            const bl_voice_t *v = voice_material(vid);
            bl_delay_t d = v ? v->delay : proj.delay[i];
            t[0] = 0;
            cat(cat(cat(cat(t, vid_name(vid)), d.enabled ? " on, " : " off, "), TIME_LABEL[d.time - 1]), ", feedback ");
            cat_u(t, d.feedback);
            cat(t, "%, decay ");
            cat_u(t, d.decay);
            cat(t, "%");
            sp_item(t, ", ");
        }
        t[0] = 0;
        cat_u(cat(t, "at "), (uint32_t)bpm_now());
        cat(t, " BPM");
        sp_item(t, ", ");
        break;
    }
    default:
        if (ui.page[V_SETUP] == 3) {
            char name[PERFORMANCE_PRESET_NAME_BYTES + 1];
            performance_name(name);
            sp_cat("Performance slot "); sp_int(ui.perf_slot + 1u);
            sp_cat(". Name "); sp_cat(performance_preset_used(ui.perf_slot + 1) == 0 && ui.perf_phase != PERF_EDIT ? "empty" : name);
            sp_cat(". "); sp_cat(PERF_OP_NAME[ui.perf_op]);
            sp_cat(". "); sp_cat(performance_status());
            sp_cat(". K1 or leave page to cancel edits. SAVE saves project and snapshots.");
            break;
        }
        if (ui.page[V_SETUP] == 1) {
            sp_cat("Global MIDI. Clock output ");
            sp_cat(DEVICE_MIDI_NAME[screen_clock().clock_out - DEVICE_MIDI_USB]);
            sp_cat(". MIDI output ");
            sp_cat(DEVICE_MIDI_NAME[screen_clock().midi_out - DEVICE_MIDI_USB]);
            break;
        }
        if (ui.page[V_SETUP] == 2) {
            int status = device_settings_status();
            sp_cat("Global theme ");
            sp_cat(DEVICE_THEME_NAME[device_settings_get().theme]);
            sp_cat(status < 0 ? ". Save failed. Turn K2 to retry." :
                status ? ". Pending save. Stop playback before power off." : ". Saved.");
            break;
        }
        sp_cat("Seed ");
        seed_hex(t, 1);
        sp_cat(t);
        if (!proj_seeded())
            sp_cat(", a new one at the first Play");
        sp_cat(". Clock ");
        sp_cat(screen_clock().sync ? "EXT" : "INT");
        sp_cat(". External input ");
        sp_cat(GLO_EXT_NAMES[port_index()]);
        sp_cat(". MIDI out ");
        sp_cat(DEVICE_MIDI_NAME[screen_clock().midi_out - DEVICE_MIDI_USB]);
        sp_cat(". Version ");
        sp_cat(BL_VERSION);
        break;
    }
    sp_flush();
}

/* a help card: its button, then its lines ("FX button. DELAY TIMES. FOR YOUR EFFECTS AT THE TEMPO") */
static void sp_line(const char *s)
{
    char p = sp_n ? sp_buf[sp_n - 1] : 0;
    if (*s == ' ')                       /* it goes on from the line before */
        s++;
    else if (p != ',' && p != ':')
        sp_cat(".");
    sp_cat(" ");
    sp_cat(s);
}
static void sp_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    sp_clear();
    sp_cat(BTN_LABEL[ui.help - 1]);
    sp_cat(" button");
    sp_cat(", help card ");
    sp_int(ui.help_part + 1u);
    sp_cat(" of ");
    sp_int((uint32_t)help_count(ui.help - 1));
    for (k = 0; k < n; k++)
        sp_line(l[k]);
}

static uint32_t sp_cell(int i)           /* a cell the knob changes, hashed (0: shown only) */
{
    cell_t c;
    if (view_knob(ui.view, i) == KN_NONE || view_knob(ui.view, i) >= KN_SHOWN)
        return 0;
    cell(ui.view, i, &c);
    return hash_s(2166136261u, c.val);
}
static void sp_snap(void)
{
    int i;
    for (i = 0; i < 4; i++)
        sp.val[i] = sp_cell(i);
    sp.view = ui.view;
    sp.page = ui.page[ui.view];
    sp.help = ui.help;
    sp.help_part = ui.help_part;
    sp.playing = (uint8_t)rt_playing();
    sp.phase = ui.st_ok && ui_st.has_arr ? ui_st.phase : 0xFFu;
}

static void speak(void)
{
    uint32_t now = plat_ms();
    int where = ann_where(), i, changed = -1;
    if (!ann_on()) {
        sp.started = 0;
        sp_said = 0;
        return;
    }
    if (!sp.started || where) {          /* everything on the screen */
        sp.started = 1;
        sp_snap();
        if (ui.help) {
            sp_help();
            ann_say(sp_buf, 0, ANN_NOW);
            sp_page(1);
            ann_say(sp_buf, 0, 0);
        } else {
            sp_page(1);
            ann_say(sp_buf, 0, ANN_NOW);
            sp_main();
        }
        sp_said = 0;
        return;
    }
    if (sp_said) {                       /* a button said what it did (and what it changed) */
        sp_said = 0;
        sp_snap();
        return;
    }
    if (rt_playing() != sp.playing) {    /* MIDI Start / Stop, the arranger's ending */
        sp.playing = (uint8_t)rt_playing();
        ann_say(sp.playing ? "PLAYING" : "STOPPED", 0, 0);
    }
    if (ui.help != sp.help || (ui.help && ui.help_part != sp.help_part)) { /* help/continuation: at once */
        sp.help = ui.help;
        sp.help_part = ui.help_part;
        if (ui.help) {
            sp_help();
            ann_say(sp_buf, 0, ANN_NOW);
            return;
        }
    }
    if (ui.view != sp.view || ui.page[ui.view] != sp.page) { /* section or remembered subpage */
        sp_snap();
        sp_page(0);
        ann_later(sp_buf, ANN_NOW, 200u, now);
        return;
    }
    for (i = 0; i < 4; i++) {            /* a knob turning */
        uint32_t v = sp_cell(i);
        if (v != sp.val[i]) {
            sp.val[i] = v;
            if (changed < 0)
                changed = i;
        }
    }
    if (changed >= 0) {
        sp_clear();
        sp_knob(ui.view, changed);
        ann_later(sp_buf, ANN_NOW, 120u, now);
    }
    i = ui.st_ok && ui_st.has_arr ? ui_st.phase : 0xFF;
    if (i != sp.phase) {                 /* the arranger moved on: queued, after what is being said */
        sp.phase = (uint8_t)i;
        if (i != 0xFF) {
            sp_clear();
            sp_cat("Phase ");
            sp_cat(PHASE_NAME[i % 7]);
            ann_say(sp_buf, 0, 0);
        }
    }
}

#ifdef PLAT_HOST
/* every page, value, help card and message as a screen reader gets them (the "speakdump" command): KIND <tab> the
 * text sent (fm1-reader/tools/audit.mjs puts it in words with web/words/ and checks it) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void sp_print_page(const char *t) { sp_print("WHERE", t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {
        {"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"}, {"STOPPED", 0}, {"PLAYING", 0}, {"B PART", "NEXT BAR"},
        {"RE-ROLL", "UNLOCKED VOICES"}, {"RE-ROLL", "WHEN PLAYING"}, {"SAVE", "SAVED"}, {"SAVE", "AT THE STOP"},
        {"UNDO", "NEXT BAR"}, {"UNDO", 0}, {"DICE", "NEXT BAR"}, {"DICE", "NEW SEED"}, {"TENSION", "NEXT BAR"},
        {">>", "NEXT BAR"}, {"SLOT 1", "STORED"}, {"SLOT 1", "NEXT BAR"}, {"SLOT 1", "WHEN PLAYING"},
        {"SCENE", "CLOSED"}, {"SCENE", "OPEN"}, {"DEGREE 1", "NEXT BAR"}, {"DEGREE 7", "WHEN PLAYING"},
        {"BASS", "MUTED"}, {"BASS", "ON"}, {"BASS", "LOCKED"}, {"BASS", "UNLOCKED"}, {"BASS", "AT ITS LOOP"},
        {"BASS", "NEXT BAR"}, {"VOICE", "HEARTBEAT"}, {"TEMPO", "120"}, {"DENSITY", "50"}, {"FULL ENSEMBLE", "NEXT BAR"},
        {"PLAYING", "AMBIENT DRIFT"}, {"RATCHET 25%", "AT NEXT PLAY"}, {"RATCHET OFF", "AT NEXT PLAY"}};
    static const char *const WARN[][2] = {
        {"SAVE", "FAILED"}, {"UNDO", "NOTHING TO UNDO"}, {"UNDO", "QUEUE FULL"}, {"UNDO", "TOO BIG"},
        {"SLOT 1", "TOO BIG"}, {"SLOT 1", "EMPTY"}, {"SLOT 1", "QUEUE FULL"}, {"DEGREE 2", "SCHEDULE FULL"},
        {"DICE", "QUEUE FULL"}, {"RE-ROLL", "QUEUE FULL"}, {"FORM", "WHEN STOPPED"}, {"AMBIENT", "WHEN STOPPED"},
        {"SYNC", "WHEN STOPPED"}, {"AUTO", "QUEUE FULL"}, {"ROOT", "QUEUE FULL"}, {"MODE", "QUEUE FULL"},
        {"DENSITY", "QUEUE FULL"}};
    proj_t keep = proj;
    const device_settings_t *keep_clock_preview = screen_clock_preview;
    device_settings_t preview_clock = screen_clock();
    screen_clock_preview = &preview_clock;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_help_part = ui.help_part, keep_sel = ui.sel;
    int v, i, x;
    char t[48];
    void (*keep_out)(const char *) = sp_out;
    for (v = 0; v < NVIEWS; v++) {       /* each page, its values and what it shows, for every voice selected */
        ui.view = (uint8_t)v;
        sp_page(1);
        sp_print("SCREEN", sp_buf);
        sp_out = sp_print_page;
        for (x = 0; x < NVKEYS; x++)     /* (the voice's own pages with each one selected) */
            if (key_voice(x) >= 0 && (x == keep_sel || v == V_VOICE || v == V_PATTERN || v == V_CC)) {
                ui.sel = (uint8_t)x;
                sp_main();
                sp_page(0);
                sp_print("SCREEN", sp_buf);
            }
        sp_out = keep_out;
        ui.sel = keep_sel;
    }
    ui.view = V_PERFORM;                 /* the values the knobs change, every one */
    for (x = 40; x <= 220; x++) {
        proj.bpm = x;
        sp_clear();
        sp_knob(V_PERFORM, 0);
        sp_print("KNOB", sp_buf);
    }
    for (x = 0; x < 4; x++) {
        proj.arr_has = (uint8_t)(x != 0);
        proj.arr_on = proj.arr_ambient = proj.arr_reprise = (uint8_t)(x & 1);
        proj.arr_min = 8, proj.arr_max = (int16_t)(16 + x);
        for (i = 1; i < 4; i++) {
            sp_clear();
            sp_knob(V_PERFORM, i);
            sp_print("KNOB", sp_buf);
        }
    }
    proj = keep;
    ui.view = V_SETUP;
    for (x = 0; x < 12; x++) {
        proj.key_root = (uint8_t)x;
        preview_clock.sync = (uint8_t)(x & 1);
        preview_clock.ext_ports = GLO_EXT_PORTS[x % 7];
        preview_clock.midi_out = (uint8_t)(DEVICE_MIDI_USB + x % 3);
        preview_clock.clock_out = (uint8_t)(DEVICE_MIDI_USB + (x + 1) % 3);
        preview_clock.lights = (uint8_t)(x % 3);
        sp_page(0);
        sp_print("KNOB", sp_buf);
    }
    proj = keep;
    preview_clock = keep_clock_preview ? *keep_clock_preview : device_settings_get();
    ui.view = V_VOICE;
    for (x = 0; x < 16; x++) {
        int k = key_voice(ui.sel);
        if (k >= 0)
            proj.channel[k] = (uint8_t)x;
        sp_clear();
        sp_knob(V_VOICE, 0);
        sp_print("KNOB", sp_buf);
    }
    proj = keep;
    ui.view = V_PATTERN;
    for (x = 0; x <= 50; x += 5) {
        proj.ratchet_pct = (uint8_t)x;
        sp_clear();
        sp_knob(V_PATTERN, 0);
        sp_print("KNOB", sp_buf);
    }
    proj = keep;
    ui.view = keep_view;
    for (i = 0; i < NB; i++) {
        for (int part = 0; part < help_count(i); part++) {
            ui.help = (uint8_t)(i + 1);
            ui.help_part = (uint8_t)part;
            sp_help();
            sp_print("HELP", sp_buf);
        }
    }
    ui.help = keep_help;
    ui.help_part = keep_help_part;
    for (i = 0; i < (int)(sizeof MSG / sizeof MSG[0]); i++) {
        msg_text(MSG[i][0], MSG[i][1], t);
        sp_print("MSG", t);
    }
    for (i = 0; i < (int)(sizeof WARN / sizeof WARN[0]); i++) {
        msg_text(WARN[i][0], WARN[i][1], t);
        sp_print("WARN", t);
    }
    for (x = 0; x < BL_NMODES; x++) {
        msg_text("MODE", MODE_NAME[x], t);
        sp_print("MSG", t);
    }
    for (x = 0; x < 12; x++) {
        msg_text("ROOT", NOTE_NAME[x], t);
        sp_print("MSG", t);
    }
    for (x = 0; x < NVKEYS; x++) {
        msg_text("VOICE", ROLE_NAME[x], t);
        sp_print("MSG", t);
    }
    for (x = 0; x < BL_NTPL; x++) {
        msg_text(TPL_NAME[x], "NEXT BAR", t);
        sp_print("MSG", t);
    }
    for (x = 0; x < 7; x++) {            /* the words the pages draw from */
        sp_clear();
        sp_cat("Phase ");
        sp_cat(PHASE_NAME[x]);
        sp_print("MSG", sp_buf);
    }
    for (x = 0; x < BL_NGENS; x++)
        sp_print("MSG", GEN_NAME[x]);
    for (x = 0; x < 3; x++)
        sp_print("MSG", DIV_NAME[x]);
    for (x = 0; x < 4; x++)
        sp_print("MSG", FORM_NAME[x][0] ? FORM_NAME[x] : "CUSTOM");
    sp_print("MSG", "PLAYING");
    sp_print("MSG", "STOPPED");
    screen_clock_preview = keep_clock_preview;
}
#endif

static void screen_init(void) { lcd_fill(0, 0, 240, 240, K_BG); }
