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
 * DELAY the Delay Cheat Sheet (DelayHint); SETUP the root, the clock, the lights and the seed. Playing, they show
 * the running engine (bl_live_status, its patterns); stopped, the transport's preview (the state its next Play
 * starts from, rt_capture) and the project's values. Everything on the screen is told to a screen reader too
 * (announce.c, fm1-reader), in the screen's own words: app/words.tsv says the short ones in full. */

static const char *const TPL_TITLE[BL_NTPL] = {"Demo trio", "Full ensemble", "Ambient drift"};   /* the Mac's */
/* by voice id (BL_VID_ order): the Mac's names (VoiceRole.displayName) and colours (VoiceColor) */
static const char *const VID_NAME[BL_NVIDS] = {"Pluck 1", "Pluck 2", "Bass", "Pad", "Lead", "Heartbeat", "Drone"};
/* the generators (BL_GEN_ order: GeneratorID's raw values, in words) */
static const char *const GEN_NAME[] = {"SEED PROCEDURE", "PULSING BASS", "STAGED PLUCK", "BASS MELODY", "CHORD LAYER",
                                       "HEARTBEAT", "DRONE"};
/* the arranger's phases (ArrangementPhase), as the Mac's panel and horizon label them */
static const char *const PHASE_NAME[7] = {"BUILD", "BED", "VAR", "B-PART", "PAYOFF", "SUBTR", "END"};

/* the screen's colours: the Mac's (DesignTokens.swift, design_language.md): its studio dark, the ink scale, the
 * tangerine accent (the page, what is being turned, what is coming), cyan for what informs (the convergence, the
 * horizon's events), amber for a lock, red for a mute */
#define K_BG RGB(19, 20, 24)             /* bgWindow */
#define K_PANEL RGB(26, 28, 33)          /* bgPanel: a card, the knob being turned */
#define K_STRIP RGB(21, 23, 27)          /* bgPanel2: the pattern strips */
#define K_STEP RGB(34, 37, 43)           /* an empty step (about the Mac's line) */
#define K_LINE RGB(44, 48, 55)           /* lineStrong: rules, a knob's track, a pip off */
#define K_TEXT RGB(231, 236, 242)        /* ink */
#define K_TEXT2 RGB(170, 178, 189)       /* ink2: values shown, not turned */
#define K_DIM RGB(126, 134, 146)         /* ink3: labels */
#define K_DIM2 RGB(96, 105, 115)         /* ink4: muted, disabled */
#define K_ACC RGB(255, 158, 74)          /* tangerine */
#define K_CYAN RGB(79, 217, 242)
#define K_AMBER RGB(246, 193, 107)
#define K_REC RGB(255, 106, 79)
static const uint16_t VID_COLOR[BL_NVIDS] = {RGB(95, 184, 201), RGB(95, 158, 201), RGB(201, 151, 95),
                                             RGB(155, 127, 201), RGB(217, 221, 227), RGB(201, 111, 111),
                                             RGB(95, 111, 168)};

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
} lane_t;
static lane_t lane[BL_MAX_VOICES];
static int nlanes;

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
    return hash(hash(hash(2166136261u, proj.tpl | (uint32_t)proj.key_root << 8 | (uint32_t)proj.key_mode << 16),
                     (uint32_t)proj.seed),
                (uint32_t)(proj.seed >> 32));
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
static const bl_vcfg_t *sel_cfg(void)   /* the selected voice in the app project's template, 0 none */
{
    int i = key_voice(ui.sel);
    return i < 0 ? 0 : &tpl_now()->voice[i];
}
static const char *vid_name(int vid) { return vid < BL_NVIDS ? VID_NAME[vid] : "Voice"; }
static uint16_t vid_color(int vid) { return vid < BL_NVIDS ? VID_COLOR[vid] : K_TEXT2; }

/* ----------------------------------------------------------- the values --- */
#define F100 F64(0x4059000000000000)    /* 100.0 */
#define STEP_TICKS (BL_PPQN / 4)         /* a 16th: the Mac's steps (VoiceLaneView.loopSteps) */

static int32_t bpm_now(void) { return rt_playing() ? rt_bpm() : proj.bpm; }   /* liveBPM ?? project.bpm */

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

/* DelayHint.compute(role:bpm:): the division by role (dotted 8th on the plucks, the heartbeat and aux; 8th on the
 * bass and the drone; quarter on the pad and the lead) and 60000 / bpm x beats ms, Int(.rounded()): to the nearest,
 * ties away. In integers, 60000 x beats over bpm rounded; equal to the Mac's binary64 sums for every tempo 40-220
 * (the host's delay_check) */
enum { DIV_DOTTED8, DIV_8TH, DIV_QUARTER };
static const char *const DIV_NAME[3] = {"dotted 8th", "8th", "quarter"};   /* DelayDivision.label */
static const uint16_t DIV_MS_BEATS[3] = {45000, 30000, 60000};            /* 60000 x 0.75, 0.5, 1.0 */
static int delay_div(int role)
{
    return role == BL_ROLE_BASS || role == BL_ROLE_DRONE  ? DIV_8TH
           : role == BL_ROLE_PAD || role == BL_ROLE_LEAD ? DIV_QUARTER
                                                          : DIV_DOTTED8;
}
static uint32_t delay_ms(int div, int32_t bpm)
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
static const char *const PORT_SHORT[3] = {"USB", "TRS", "USB + TRS"};
static const char *const LIGHT_NAME[3] = {"OFF", "ON", "KEYS"};

static int port_index(void) { return proj.ext_ports == 1u << PORT_TRS ? 1 : proj.ext_ports == 1u << PORT_USB ? 0 : 2; }

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

static void cell(int view, int i, cell_t *c)
{
    const bl_vcfg_t *vc = sel_cfg();
    const lane_t *l = sel_lane();
    int k = VIEW_KNOB[view][i], v = key_voice(ui.sel);
    memset(c, 0, sizeof *c);
    c->kind = k < KN_SHOWN ? CELL_PIPS : CELL_TEXT;
    c->hi = 1;
    switch (k) {
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
        c->hi = 15, c->v = v < 0 ? -1 : proj.channel[v];
        cat(c->val, "-");
        if (v >= 0)
            itoa_u(proj.channel[v] + 1u, c->val);
        break;
    case KN_ROOT:
        c->kind = CELL_RING;
        c->hi = 11, c->v = proj.key_root;
        cat(c->val, NOTE_NAME[proj.key_root]);
        break;
    case KN_SYNC:
        c->v = proj.sync;
        cat(c->val, proj.sync ? "EXT" : "INT");
        break;
    case KN_PORT:
        c->hi = 2, c->v = (int16_t)port_index();
        cat(c->val, PORT_SHORT[c->v]);
        break;
    case KN_LIGHTS:
        c->hi = 2, c->v = proj.lights;
        cat(c->val, LIGHT_NAME[proj.lights % 3u]);
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
    case KNS_VOICE:
        if (key_voice(ui.sel) >= 0)
            caps(c->val, vid_name(key_vid(ui.sel)), sizeof c->val);
        else
            cat(c->val, "-");
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
static void ring(int32_t cx, int32_t cy, int32_t r, int32_t v, uint16_t track, uint16_t fill, uint16_t bg)
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

/* a tag: s on a tint of its colour, h px high (its text FONT_T); returns its width */
static int32_t tag(int32_t x, int32_t y, int32_t h, const char *s, uint16_t c)
{
    int32_t w = text_w(&FONT_T, s) + 6;
    cv_round(x, y, w, h, 2, mix(K_BG, c, 4));
    cv_text(x + 3, y + (h - 12) / 2, &FONT_T, s, c);
    return w;
}

/* a horizon chip (HorizonStripView's): a, a dot, b, on a tint of its colour, FONT_XS; its width, nothing drawn when
 * draw is 0 */
static int32_t chip(int32_t x, int32_t y, const char *a, const char *b, uint16_t c, int draw)
{
    int32_t wa = text_w(&FONT_XS, a), w = wa + 10 + (b && b[0] ? 9 + text_w(&FONT_XS, b) : 0);
    if (!draw)
        return w;
    cv_round(x, y, w, 15, 6, mix(K_BG, c, 3));
    cv_text(x + 5, y, &FONT_XS, a, c);
    if (b && b[0]) {
        cv_rect(x + 5 + wa + 3, y + 7, 2, 2, c);
        cv_text(x + 5 + wa + 9, y, &FONT_XS, b, c);
    }
    return w;
}

/* a pattern strip (PatternStripView): the beats, the notes by position, length and pitch (self-scaling to the
 * pattern's range, in the key at MIDI 60, the Mac's referenceC), the accents' tops tangerine, the voice's own
 * playhead (its loop position: the phasing, visible); dimmed when muted, waiting or blanked */
static void strip(int32_t x, int32_t y, int32_t w, int32_t h, const lane_t *l, uint16_t c, int dim)
{
    const bl_pattern_t *p = l->pat;
    int32_t len = (int32_t)p->length, lo = 127, hi = 0, span, nh = h >= 24 ? 4 : 2, i, j;
    int32_t pitch[BL_MAX_NOTES];
    bl_key_t k;
    uint16_t nc = dim || l->blank ? mix(K_STRIP, c, 6) : c;
    cv_round(x, y, w, h, 2, K_STRIP);
    if (len <= 0)
        return;
    if (len / BL_PPQN * 4 <= w)          /* the beats, 4 px apart or more */
        for (j = BL_PPQN; j < len; j += BL_PPQN)
            cv_rect(x + j * w / len, y + 1, 1, h - 2, mix(K_STRIP, K_LINE, 10));
    key_now(&k);
    for (i = 0; i < p->n; i++) {
        pitch[i] = bl_resolve(&k, p->note[i].pitch, 60);
        lo = pitch[i] < lo ? pitch[i] : lo;
        hi = pitch[i] > hi ? pitch[i] : hi;
    }
    lo -= 2;
    hi += 2;
    span = hi - lo < 5 ? 5 : hi - lo;
    for (i = 0; i < p->n; i++) {
        int32_t pos = (int32_t)p->note[i].position, g = p->note[i].gate < len ? (int32_t)p->note[i].gate : len;
        int32_t nx = x + pos * w / len, nw = g * w / len, ny = y + 1 + (hi - pitch[i]) * (h - 2 - nh) / span;
        nw = nw < 2 ? 2 : nx + nw > x + w ? x + w - nx : nw;
        cv_rect(nx, ny, nw, nh, nc);
        if (p->note[i].accent && !dim)
            cv_rect(nx, ny, nw, 1, K_ACC);
    }
    if (ui.st_ok) {                      /* the playhead: (current + startOffset) % length */
        int32_t at = (int32_t)((rt_play_tick() + p->start_offset) % p->length);
        cv_rect(x + at * w / len, y, 1, h, K_ACC);
    }
}

/* the legitimization countdown (LifecycleDotsView): filled, the verbatim plays left (up to 4; amber when locked);
 * all empty, established (mutable): cyan rings */
static void dots(int32_t x, int32_t y, const lane_t *l)
{
    int32_t j, n = l->remaining > 4 ? 4 : l->remaining > 0 ? l->remaining : 0;
    for (j = 0; j < 4; j++) {
        if (j < n) {
            cv_round(x + j * 6, y, 4, 4, 1, l->locked ? K_AMBER : K_TEXT2);
        } else {
            cv_round(x + j * 6, y, 4, 4, 1, n ? K_DIM2 : mix(K_BG, K_CYAN, 9));
            cv_rect(x + j * 6 + 1, y + 1, 2, 2, K_BG);
        }
    }
}

/* the voice's marks, the most telling first: WAITS (the arranger holds it back), MUTE, LOCK, BLANK; n of them */
static int marks(const lane_t *l, const char *m[4], uint16_t c[4])
{
    int n = 0;
    if (!l->active)
        m[n] = "WAITS", c[n++] = K_ACC;
    if (rt_muted(l->vid))
        m[n] = "MUTE", c[n++] = K_REC;
    if (l->locked)
        m[n] = "LOCK", c[n++] = K_AMBER;
    if (l->blank)
        m[n] = "BLANK", c[n++] = K_CYAN;
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
    uint32_t h = hash(hash(2166136261u, proj.tpl | (uint32_t)play << 4 | (uint32_t)msg << 5 | (uint32_t)proj.sync << 6 |
                                            (uint32_t)beat << 7 | (uint32_t)proj_save_pending() << 8 |
                                            (uint32_t)proj.key_root << 12 | (uint32_t)proj.key_mode << 16),
                      (uint32_t)bpm_now());
    if (msg)
        h = hash_s(hash_s(hash(h, ui.msg_until), ui.msg[0]), ui.msg[1]);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], K_ACC);
    } else {
        int32_t x = 232, w;
        const felucca_font_t *f;
        if (proj_save_pending()) {       /* a SAVE waiting for the stop */
            cv_round(x - 5, 12, 5, 5, 2, K_ACC);
            x -= 11;
        }
        if (proj.sync)
            caps(t, "EXT", sizeof t);
        else
            itoa_u((uint32_t)bpm_now(), t);
        x -= text_w(&FONT_S, t);
        cv_text(x, 6, &FONT_S, t, play ? K_TEXT : K_DIM);
        x -= 16;
        if (play) {                      /* the beat */
            cv_round(x, 9, 10, 10, 5, beat ? K_ACC : mix(K_BG, K_ACC, 5));
        } else {
            int32_t j;                   /* stopped: a play triangle */
            for (j = 0; j < 5; j++)
                cv_rect(x + 2 + j, 9 + j, 1, 11 - 2 * j, K_DIM);
        }
        w = text_w(&FONT_B, TPL_TITLE[proj.tpl]) + 12;   /* the template, in a tag */
        cv_round(4, 4, w, 20, 6, K_TEXT);
        cv_text(10, 6, &FONT_B, TPL_TITLE[proj.tpl], K_BG);
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
        hz[n++].c = K_CYAN;
    }
    if (d) {
        cat(hz[n].a, "DEG ");
        cat_u(hz[n].a, (uint32_t)d);
        cat(hz[n].b, "next bar");
        hz[n++].c = K_ACC;
    }
    if (ui.st_ok && ui_st.has_arr) {
        cat(hz[n].a, PHASE_NAME[ui_st.phase % 7u]);
        cat(hz[n].b, "change in ~");
        bar_count(ui_st.bars_until_change > 0 ? ui_st.bars_until_change : 0, hz[n].b);
        hz[n++].c = K_ACC;
    }
    for (i = 0; ui.st_ok && i < ui_st.nup && i < 6; i++) {
        const bl_upcoming_t *u = &ui_st.up[i];
        char *a = hz[n].a;
        switch (u->kind) {
        case BL_UP_KEY: cat(cat(a, "KEY "), NOTE_NAME[u->root % 12]); break;
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
        hz[n++].c = K_CYAN;
    }
    for (i = 0; i < nlanes; i++) {       /* the nearest mutation: an active, unlocked voice still repeating */
        const lane_t *l = &lane[i];
        int32_t per = (int32_t)(l->pat->length / BL_TICKS_PER_BAR), bars;
        if (!l->active || l->locked || l->remaining < 0)
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
        cv_text(10, ty, &FONT_T, t, dim ? K_DIM2 : K_TEXT);
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
        t[0] = 0;
        cat_u(t, (uint32_t)steps(l->pat));
        text_r(211, ty, &FONT_T, t, K_DIM);
        dots(214, y + (sh - 4) / 2, l);
    }
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
    cv_text(24, 127, &FONT_XS, t, K_CYAN);
    t[0] = 0;
    cat_u(t, (uint32_t)tension_left());
    x = 236 - text_w(&FONT_XS, t);
    cv_text(x, 127, &FONT_XS, t, tension_left() > 0 ? K_TEXT2 : K_DIM2);
    x -= text_w(&FONT_T, "TENSION") + 4;
    cv_text(x, 129, &FONT_T, "TENSION", K_DIM);
    t[0] = 0;
    cat_u(t, (uint32_t)degree_now());
    x -= text_w(&FONT_XS, t) + 10;
    cv_text(x, 127, &FONT_XS, t, degree_now() == 1 ? K_TEXT2 : K_ACC);
    x -= text_w(&FONT_T, "DEG") + 4;
    cv_text(x, 129, &FONT_T, "DEG", K_DIM);
}

/* a row of a page: a label (FONT_T, dim) and its value (FONT_XS) */
static void row(int32_t x, int32_t y, int32_t vx, const char *label, const char *val, uint16_t c)
{
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
    caps(t, vid_name(vid), sizeof t);
    cv_text(6, y, f, t, l && lane_dim(l) ? K_DIM2 : vid_color(vid));
    for (j = n - 1; j >= 0; j--) {
        x -= text_w(&FONT_T, m[j]) + 6;
        tag(x, y + (f->h - 14) / 2, 14, m[j], mc[j]);
        x -= 4;
    }
    if (!l) {
        x -= text_w(&FONT_T, "NOT PLAYING") + 6;
        tag(x, y + (f->h - 14) / 2, 14, "NOT PLAYING", K_DIM);
    }
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
    row(6, 99, 84, "LIFECYCLE", t, l->locked ? K_AMBER : K_TEXT2);
    t[0] = 0;
    cat(t, l->pending == BL_REQ_REGENERATE ? "regenerate" : l->pending == BL_REQ_BLANK ? "a blank cycle" : "-");
    row(6, 114, 84, "AT ITS LOOP", t, l->pending ? K_ACC : K_TEXT2);
    t[0] = 0;
    if (vc->ncc) {
        caps(t, vc->cc_name[0], 16);
        cat_u(cat(t, " CC "), vc->cc_num[0]);
        if (!vc->nlanes)
            cat(t, ", no lane");
    } else {
        cat(t, "none");
    }
    row(6, 129, 84, "CC", t, K_TEXT2);
}

/* ---- PATTERN: the selected voice's pattern in steps, a bar a row: an onset lit (its degree in it where there is
 * room; an accent tangerine), what its gate holds dim, the playhead's step ringed */
static void draw_pattern(void)
{
    const lane_t *l = sel_lane();
    const bl_pattern_t *p;
    int32_t n, rows, rh, i, s;
    uint8_t on[256], acc[256], deg[256];
    uint16_t c;
    char t[24];
    bl_key_t k;
    voice_title(&FONT_B, 2);
    if (!l)
        return;
    p = l->pat;
    key_now(&k);
    n = steps(p);
    n = n > 256 ? 256 : n;
    c = vid_color(l->vid);
    memset(on, 0, sizeof on);
    memset(acc, 0, sizeof acc);
    memset(deg, 0, sizeof deg);
    for (i = 0; i < p->n; i++) {         /* 1 an onset, 2 held */
        int32_t a = (int32_t)(p->note[i].position / STEP_TICKS), b = (int32_t)((p->note[i].position + p->note[i].gate - 1) / STEP_TICKS);
        for (s = a + 1; s <= b && s < n; s++)
            if (!on[s])
                on[s] = 2;
        if (a < n && on[a] != 1) {       /* (a chord: its first note's degree) */
            on[a] = 1;
            acc[a] = p->note[i].accent;
            deg[a] = (uint8_t)degree_label(&p->note[i], &k);
        }
    }
    rows = (n + 15) / 16;
    rh = rows ? 116 / rows : 116;
    rh = rh > 16 ? 16 : rh;
    for (s = 0; s < n; s++) {
        int32_t x = 8 + (s % 16) * 14, y = 24 + (s / 16) * rh, h = rh - 2;
        uint16_t f = on[s] == 1 ? (lane_dim(l) || l->blank ? mix(K_STEP, c, 8) : c) : on[s] ? mix(K_STEP, c, 5) : K_STEP;
        cv_round(x, y, 12, h, h >= 8 ? 2 : 0, f);
        if (on[s] == 1 && acc[s])
            cv_rect(x, y, 12, 2, K_ACC);
        if (on[s] == 1 && h >= 13) {
            t[0] = (char)('0' + deg[s]);
            t[1] = 0;
            text_c(x + 6, y + (h - 12) / 2, &FONT_T, t, K_BG);
        }
        if (s % 4 == 0 && s % 16)        /* the beats */
            cv_rect(x - 2, y + h / 2, 1, 1, K_DIM2);
    }
    if (ui.st_ok && n) {
        s = (int32_t)(((rt_play_tick() + p->start_offset) % p->length) / STEP_TICKS);
        if (s < n) {
            int32_t x = 8 + (s % 16) * 14, y = 24 + (s / 16) * rh, h = rh - 2;
            cv_rect(x - 1, y - 1, 14, 1, K_ACC);
            cv_rect(x - 1, y + h, 14, 1, K_ACC);
            cv_rect(x - 1, y, 1, h, K_ACC);
            cv_rect(x + 12, y, 1, h, K_ACC);
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
            cv_rect(6 + (int32_t)(rt_play_tick() % cyc) * 228 / (int32_t)cyc, 22, 1, 58, K_ACC);
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
        x += chip(x, 119, t, 0, on ? K_ACC : K_TEXT2, 1) + 4;
    }
}

/* ---- ARRANGER: AUTO and the form, the phases (the one now lit), where it is and what comes, the voices in, the
 * density and the form's settings */
static void draw_arranger(void)
{
    static const char *const FORM_TITLE[] = {"custom", "classic arc", "slow build", "ambient plateau"};
    char t[48], u[16];
    int i, live = ui.st_ok && ui_st.has_arr;
    int32_t x;
    x = 4 + tag(4, 3, 15, "AUTO", proj.arr_on ? K_ACC : K_DIM2) + 6;
    x = cv_text(x, 3, &FONT_XS, proj.arr_has ? FORM_TITLE[proj_form()] : "no arrangement", proj.arr_has ? K_TEXT : K_DIM);
    if (proj.arr_ambient)
        tag(x + 6, 3, 15, "AMBIENT", K_CYAN);
    for (i = 0, x = 4; i < 7; i++) {     /* the phases, in their order */
        int32_t w = text_w(&FONT_T, PHASE_NAME[i]) + 6;
        int now = live && ui_st.phase == i, past = live && i < ui_st.phase;
        cv_round(x, 26, w, 15, 2, now ? K_ACC : K_PANEL);
        cv_text(x + 3, 28, &FONT_T, PHASE_NAME[i], now ? K_BG : past ? K_DIM2 : K_TEXT2);
        x += w + 3;
    }
    t[0] = 0;
    if (live) {
        bar_count(ui_st.bars_in_phase, t);
        cat(cat(t, " in "), PHASE_NAME[ui_st.phase % 7u]);
        cat(t, ", change in ~");
        bar_count(ui_st.bars_until_change > 0 ? ui_st.bars_until_change : 0, t);
    } else if (ui.st_ok || !proj.arr_has) {
        cat(t, proj.arr_has ? "-" : "AUTO starts one");
    } else {
        cat(t, proj.arr_on ? "from the next Play" : "off: the next Play is yours");
    }
    cv_text(4, 46, &FONT_XS, t, live ? K_TEXT : K_DIM);
    cv_text(4, 68, &FONT_T, "IN", K_DIM);
    for (i = 0, x = 24; live && i < ui_st.nactive; i++) {   /* the voices the arranger has in */
        int vid = ui_st.active[i] < nlanes ? lane[ui_st.active[i]].vid : BL_VID_NONE;
        caps(u, vid_name(vid), sizeof u);
        if (x + text_w(&FONT_T, u) + 6 > 236)
            break;
        x += tag(x, 66, 14, u, vid_color(vid)) + 3;
    }
    if (!live)
        cv_text(24, 66, &FONT_XS, ui.st_ok ? "every voice" : "-", K_DIM);
    t[0] = 0;
    cat_u(t, ui.st_ok ? pct(ui_st.density) : proj.density_pct);
    cat(t, "%");
    row(4, 92, 64, "DENSITY", t, K_TEXT2);
    t[0] = 0;
    if (proj.arr_has) {
        cat_u(t, (uint32_t)proj.arr_min);
        cat_u(cat(t, "-"), (uint32_t)proj.arr_max);
        cat(t, " bars");
    } else {
        cat(t, "-");
    }
    row(4, 107, 64, "LENGTH", t, K_TEXT2);
    row(4, 122, 64, "REPRISE", proj.arr_has && proj.arr_reprise ? "on" : "off", K_TEXT2);
    row(124, 122, 184, "BED DROP", "off", K_TEXT2);
}

/* ---- DELAY: the Delay Cheat Sheet: for each of the template's voices its division and time at the tempo shown */
static void draw_delay(void)
{
    const bl_project_t *p = tpl_now();
    const felucca_font_t *f = p->nvoices > 6 ? &FONT_T : &FONT_XS;
    int32_t rh = p->nvoices > 6 ? 12 : 14, i, y;
    char t[40];
    cv_text(4, 2, &FONT_B, "DELAY CHEAT SHEET", K_ACC);
    t[0] = 0;
    cat_u(cat(t, "audio delay per voice @ "), (uint32_t)bpm_now());
    cat(t, " BPM");
    cv_text(4, 20, &FONT_XS, t, K_DIM);
    cv_text(4, 40, &FONT_T, "VOICE", K_DIM);
    cv_text(96, 40, &FONT_T, "DIVISION", K_DIM);
    text_r(236, 40, &FONT_T, "TIME", K_DIM);
    cv_rect(4, 53, 232, 1, K_LINE);
    for (i = 0, y = 56; i < p->nvoices; i++, y += rh) {
        int div = delay_div(p->voice[i].role);
        t[0] = 0;
        cat_u(t, delay_ms(div, bpm_now()));
        cat(t, " ms");
        cv_text(4, y, f, vid_name(bl_vid(p->voice[i].id)), K_TEXT);
        cv_text(96, y, f, DIV_NAME[div], K_CYAN);
        text_r(236, y, f, t, K_TEXT2);
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

/* ---- SETUP: the template, the key, the clock, MIDI out, the version; the seed as 16 hex digits (a run is
 * reproducible on the Mac with it) */
static void draw_setup(void)
{
    char t[40];
    int32_t x, i;
    row(4, 4, 76, "TEMPLATE", TPL_TITLE[proj.tpl], K_TEXT);
    t[0] = 0;
    cat(cat(cat(t, NOTE_NAME[proj.key_root]), " "), MODE_NAME[proj.key_mode]);
    row(4, 20, 76, "KEY", t, K_TEXT);
    t[0] = 0;
    if (proj.sync)
        cat(cat(t, "EXT from "), PORT_SHORT[port_index()]);
    else
        cat(t, "INT, sent on USB");
    row(4, 36, 76, "CLOCK", t, K_TEXT);
    row(4, 52, 76, "MIDI OUT", "USB", K_TEXT);
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
    cv_round(16, 12, 208, 124, 6, K_PANEL);   /* (four lines at most) */
    cv_round(16, 12, 208, 26, 6, K_ACC);
    cv_rect(16, 32, 208, 6, K_ACC);
    text_c(120, 16, &FONT_B, BTN_LABEL[ui.help - 1], K_BG);
    for (k = 0; k < n; k++)
        text_c(120, 50 + k * 20, &FONT_S, l[k] + (l[k][0] == ' '), K_TEXT);
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
        return hash(0x48454C50u, ui.help);
    h = hash(hash(h, ui.pre_sig), (uint32_t)proj.arr_has | proj.arr_on << 1 | proj.arr_ambient << 2 |
                                      proj.arr_reprise << 3 | (uint32_t)proj.arr_min << 8 | (uint32_t)proj.arr_max << 20);
    h = hash(hash(h, proj.sync | proj.ext_ports << 1 | (uint32_t)proj.density_pct << 8 | (uint32_t)proj_seeded() << 16 |
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
    }
    for (k = 0; k < nlanes; k++) {       /* the patterns: a mutation changes them */
        const bl_pattern_t *p = lane[k].pat;
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
        case V_PERFORM: draw_perform(); break;
        case V_VOICE: draw_voice(); break;
        case V_PATTERN: draw_pattern(); break;
        case V_CC: draw_cc(); break;
        case V_ARRANGER: draw_arranger(); break;
        case V_DELAY: draw_delay(); break;
        default: draw_setup(); break;
        }
    cv_blit(0, MAIN_Y);
}

/* the knob strip (AMB-1's): a rule with the page's name and seven page dots at its right end; under it a cell a
 * knob: its name, a ring (or pips, for a switch of a few positions) and the value where the knob changes it, the
 * value alone (and a line under it) where it is shown only. The knob being turned is drawn on a tint */
static void draw_knobs(void)
{
    cell_t c[4];
    int now_touch = plat_ms() < ui.touch_until, i;
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)(ui.touched + 1) << 8 | (uint32_t)now_touch << 16);
    for (i = 0; i < 4; i++) {
        cell(ui.view, i, &c[i]);
        h = hash_s(hash_s(hash(h, (uint32_t)c[i].v), c[i].val), c[i].sub);
    }
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    {   /* the rule; at its right end, where you are: the page, then the dots */
        char t[12];
        int k;
        int32_t xd = 237 - NVIEWS * 6, xt;
        caps(t, VIEW_NAME[ui.view], sizeof t);
        xt = xd - 5 - text_w(&FONT_XS, t);
        cv_rect(4, 5, xt - 9, 1, K_LINE);
        cv_text(xt, -1, &FONT_XS, t, K_ACC);
        for (k = 0; k < NVIEWS; k++)
            cv_round(xd + k * 6, 3, 4, 4, 2, k == ui.view ? K_ACC : K_LINE);
    }
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], hot = now_touch && ui.touched == i && k < KN_SHOWN;
        int32_t x = i * 60, cx = x + 30;
        char t[16];
        const felucca_font_t *f;
        uint16_t bg = hot ? K_PANEL : K_BG;
        if (i)
            cv_rect(x, 16, 1, 46, K_LINE);
        if (hot)
            cv_round(x + 3, 11, 54, 57, 6, K_PANEL);
        caps(t, KNOB_LABEL[ui.view][i], sizeof t);
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
                cv_round(cx - w0 / 2 + j * (pw + gap), 33, pw, 8, 2, j == c[i].v - c[i].lo ? K_ACC : K_LINE);
        } else {
            ring(cx, 37, 12, c[i].v < c[i].lo ? 0 : (c[i].v - c[i].lo) * 1024 / (c[i].hi - c[i].lo), K_LINE, K_ACC, bg);
        }
        f = fit(&FONT_B, c[i].val, 56);
        text_c(cx, f == &FONT_T ? 52 : 50, f, c[i].val, hot ? K_ACC : K_TEXT);
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
    uint8_t view, help, started, playing, phase;
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
    static const uint8_t WITH_SUB[] = {KNS_LOOP, KNS_GEN, KNS_TARGET, KNS_CYCLE, KNS_LENGTH, KNS_PHASE, KNS_PLAYS};
    cell_t c;
    uint32_t j;
    int k = VIEW_KNOB[view][i];
    cell(view, i, &c);
    sp_cat(KNOB_LABEL[view][i]);
    sp_cat(" ");
    if (k == KNS_REGISTER && sel_cfg()) {   /* (its notes may have a minus of their own) */
        char t[24];
        reg_text(sel_cfg()->reg_low, sel_cfg()->reg_high, t, 1);
        sp_cat(t);
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

/* the page: its name and its values; all: the header too (the template, its key, the tempo, playing or stopped) */
static void sp_page(int all)
{
    int i;
    sp_clear();
    sp_cat(VIEW_NAME[ui.view]);
    sp_cat(" page");
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
    if (proj.sync)
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
    case V_PATTERN: {
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
        if (ui.view == V_PATTERN && l->pat->n) {   /* the onsets' steps, where they are few enough to hear */
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
    case V_ARRANGER:
        sp_cat(proj.arr_on ? "Auto on" : "Auto off");
        if (ui.st_ok && ui_st.has_arr) {
            sp_cat(". Phase ");
            sp_cat(PHASE_NAME[ui_st.phase % 7u]);
            sp_cat(", ");
            t[0] = 0;
            bar_count(ui_st.bars_in_phase, t);
            sp_cat(t);
            sp_cat(" in, change in about ");
            t[0] = 0;
            bar_count(ui_st.bars_until_change > 0 ? ui_st.bars_until_change : 0, t);
            sp_cat(t);
            sp_cat(". In");
            for (i = 0; i < ui_st.nactive; i++) {
                sp_cat(i ? ", " : ": ");
                sp_cat(vid_name(ui_st.active[i] < nlanes ? lane[ui_st.active[i]].vid : BL_VID_NONE));
            }
        }
        sp_cat(". Density ");
        sp_int(ui.st_ok ? pct(ui_st.density) : proj.density_pct);
        sp_cat("%");
        break;
    case V_DELAY: {
        const bl_project_t *p = tpl_now();
        for (i = 0; i < p->nvoices; i++) {
            int div = delay_div(p->voice[i].role);
            t[0] = 0;
            cat(cat(cat(cat(t, vid_name(bl_vid(p->voice[i].id))), " "), DIV_NAME[div]), " ");
            cat_u(t, delay_ms(div, bpm_now()));
            cat(t, " ms");
            sp_item(t, ", ");
        }
        t[0] = 0;
        cat_u(cat(t, "at "), (uint32_t)bpm_now());
        cat(t, " BPM");
        sp_item(t, ", ");
        break;
    }
    default:
        sp_cat("Seed ");
        seed_hex(t, 1);
        sp_cat(t);
        if (!proj_seeded())
            sp_cat(", a new one at the first Play");
        sp_cat(". Clock ");
        sp_cat(proj.sync ? "EXT" : "INT");
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
    for (k = 0; k < n; k++)
        sp_line(l[k]);
}

static uint32_t sp_cell(int i)           /* a cell the knob changes, hashed (0: shown only) */
{
    cell_t c;
    if (VIEW_KNOB[ui.view][i] >= KN_SHOWN)
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
    sp.help = ui.help;
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
    if (ui.help != sp.help) {            /* a help card: at once */
        sp.help = ui.help;
        if (ui.help) {
            sp_help();
            ann_say(sp_buf, 0, ANN_NOW);
            return;
        }
    }
    if (ui.view != sp.view) {            /* a new page: said when it settles */
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
        {"PLAYING", "AMBIENT DRIFT"}};
    static const char *const WARN[][2] = {
        {"SAVE", "FAILED"}, {"UNDO", "NOTHING TO UNDO"}, {"UNDO", "QUEUE FULL"}, {"UNDO", "TOO BIG"},
        {"SLOT 1", "TOO BIG"}, {"SLOT 1", "EMPTY"}, {"SLOT 1", "QUEUE FULL"}, {"DEGREE 2", "SCHEDULE FULL"},
        {"DICE", "QUEUE FULL"}, {"RE-ROLL", "QUEUE FULL"}, {"FORM", "WHEN STOPPED"}, {"AMBIENT", "WHEN STOPPED"},
        {"SYNC", "WHEN STOPPED"}, {"AUTO", "QUEUE FULL"}, {"ROOT", "QUEUE FULL"}, {"MODE", "QUEUE FULL"},
        {"DENSITY", "QUEUE FULL"}};
    proj_t keep = proj;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_sel = ui.sel;
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
        proj.sync = (uint8_t)(x & 1);
        proj.ext_ports = (uint8_t)(x % 3 == 0 ? 1u << PORT_USB : x % 3 == 1 ? 1u << PORT_TRS : 3u);
        proj.lights = (uint8_t)(x % 3);
        sp_page(0);
        sp_print("KNOB", sp_buf);
    }
    proj = keep;
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
    ui.view = keep_view;
    for (i = 0; i < NB; i++) {
        const char *l[HELP_LINES];
        if (!help_lines(i, l))
            continue;
        ui.help = (uint8_t)(i + 1);
        sp_help();
        sp_print("HELP", sp_buf);
    }
    ui.help = keep_help;
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
        sp_print("MSG", GEN_NAME[x]);
    }
    for (x = 0; x < 3; x++)
        sp_print("MSG", DIV_NAME[x]);
    for (x = 0; x < 4; x++)
        sp_print("MSG", FORM_NAME[x][0] ? FORM_NAME[x] : "CUSTOM");
    sp_print("MSG", "PLAYING");
    sp_print("MSG", "STOPPED");
}
#endif

static void screen_init(void) { lcd_fill(0, 0, 240, 240, K_BG); }
