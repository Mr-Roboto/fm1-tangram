/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan UI: the panel and the screen (the spec's Controls). Part of the unity build (after runtime.c and
 * project.c); the host simulator includes it too.
 *
 *   F3-B3          the voices in role order: Pluck 1, Pluck 2, Bass, Pad, Lead, Drone, Heartbeat. A tap mutes and
 *                  unmutes (a voice the arranger holds back, WAITS: brings it in); held, the voice is selected, and
 *                  with EDIT / REC / SEL it is regenerated / blanked for a cycle / locked
 *   C4-B4 white    the degrees 1-7, at the next bar      C#4 D#4 F#4 G#4 A#4   the template's CC scenes, over 8 bars
 *   C5-F5          the snapshot slots 1-6: recalled at the next bar; SAVE + a slot: stored
 *   F#5            >>: the arranger's next change         G5   one unit of tension
 *   PLAY           play / stop             REC   the B part          EDIT   re-roll every unlocked voice
 *   SEL            lock (on ENV) SAVE saves     OCT- mutate (existing re-roll), OCT+ Dice
 *   HOME + OCT-    undo, momentary: both button releases are consumed
 *   HOME ENV ARP LFO SEQ FX GLO   QUICK/PLAY, TIMING/VOICE, HARMONY, CC, MOTION, MIDI DELAY, SETUP
 *   MASTER         density                 SELECT the voice (HOME held: tempo)
 *   ALGORITHM      COMPOSE stage; mode outside HOME   PRESETS the template
 *   K1-K4          the current page's values; LFO/GLO retain their existing bindings
 *
 * Every action takes the Mac's way (UIState, AppServices): the engine's action at the render head (rt_action), the
 * transport's mute, the project's change (proj_*), an undo point first where UIState pushes one. What is refused
 * says so (the engine's queue or the transposition schedule full, a state too big for a slot or an undo point).
 *
 * The screen, its pages and the screen reader are screen.c's. */

/* the pages, a button each */
enum { V_PERFORM, V_VOICE, V_PATTERN, V_CC, V_ARRANGER, V_DELAY, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"HOME QUICK", "ENV TIMING", "ARP HARMONY", "LFO EDIT", "SEQ MOTION", "FX MIDI DELAY", "GLO SETUP"};
static const uint8_t VIEW_PAGES[NVIEWS] = {3, 2, 1, 1, 1, 2, 4};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_ARP, B_LFO, B_SEQ, B_FX, B_GLO};
/* Page bindings: KN_ edits through the project interfaces; KNS_ remains shown
 * only. Legacy AUTO/FORM/AMBIENT/RATCHET handlers remain for later placement. */
enum {
    KN_NONE, KN_TEMPO, KN_AUTO, KN_FORM, KN_AMBIENT, KN_CHANNEL, KN_ROOT, KN_SYNC, KN_PORT, KN_LIGHTS, KN_RATCHET,
    KN_MIDI_OUT, KN_CLOCK_OUT,
    KN_TRACK, KN_GENERATOR, KN_VROOT, KN_SCALE, KN_DRUM_PARTS, KN_DRUM_GROOVE,
    KN_DIRECTION, KN_GROOVE, KN_DELAY_ON, KN_DELAY_TIME,
    KN_RATE, KN_GATE, KN_LOOP, KN_PHASE, KN_REGISTER, KN_FEEDBACK, KN_VDENSITY, KN_DECAY,
    KN_COMPOSER, KN_COMP_BARS, KN_COMP_EVOLUTION, KN_COMP_TENSION,
    KN_THEME, KN_DEVICE_RETRY,
    KN_LFO_TARGET, KN_LFO_CYCLE, KN_LFO_DEPTH, KN_LFO_CENTER,
    KN_MIX_VOLUME, KN_MIX_PAN, KN_MIX_CUTOFF, KN_MIX_DECAY,
    KN_PERF_SLOT, KN_PERF_OP, KN_PERF_POS, KN_PERF_CHAR,
    KNS_LOOP, KNS_REGISTER, KNS_GEN, KNS_TARGET, KNS_CYCLE, KNS_DEPTH, KNS_SCENE, KNS_PHASE, KNS_LENGTH, KNS_DROP,
    KNS_REPRISE, KNS_STEPS, KNS_GENERATION, KNS_PLAYS, KNS_FEEDBACK, KNS_WET, KNS_PINGPONG, KNS_FILTERED
};
#define KN_SHOWN KNS_LOOP                /* from here on: shown only */
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {KN_TRACK, KN_GENERATOR, KN_VROOT, KN_SCALE},
    {KN_RATE, KN_GATE, KN_LOOP, KN_PHASE},
    {KN_GENERATOR, KN_VROOT, KN_SCALE, KN_DIRECTION},
    {KN_LFO_TARGET, KN_LFO_CYCLE, KN_LFO_DEPTH, KN_LFO_CENTER},
    {KN_GROOVE, KN_LOOP, KN_PHASE, KN_DIRECTION},
    {KN_DELAY_ON, KN_DELAY_TIME, KN_FEEDBACK, KN_DECAY},
    {KN_ROOT, KN_SYNC, KN_PORT, KN_LIGHTS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Track", "Generator", "Root", "Scale"},
    {"Rate", "Gate", "Loop length", "Phase"},
    {"Generator", "Root", "Scale", "Direction"},
    {"Target", "Cycle", "Depth", "Center"},
    {"Groove", "Loop length", "Phase", "Direction"},
    {"Delay", "Time", "Feedback", "Decay"},
    {"Root", "Sync", "EXT input", "Lights"},
};
static const uint8_t PLAY_KNOB[4] = {KN_DIRECTION, KN_GROOVE, KN_DELAY_ON, KN_DELAY_TIME};
static const uint8_t VOICE_KNOB[4] = {KN_CHANNEL, KN_REGISTER, KN_VDENSITY, KN_COMP_TENSION};
static const uint8_t COMPOSER_KNOB[4] = {KN_COMPOSER, KN_FORM, KN_COMP_BARS, KN_COMP_EVOLUTION};
static const uint8_t MIXER_KNOB[4] = {KN_MIX_VOLUME, KN_MIX_PAN, KN_MIX_CUTOFF, KN_MIX_DECAY};
static const char *const MIXER_LABEL[4] = {"Volume CC7", "Pan CC10", "Cutoff CC74", "Decay CC75"};
static const uint8_t MIDI_KNOB[4] = {KN_CLOCK_OUT, KN_MIDI_OUT, KN_NONE, KN_NONE};
static const char *const MIDI_LABEL[4] = {"Clock out", "MIDI out", "Unused", "Unused"};
static const uint8_t APPEARANCE_KNOB[4] = {KN_THEME, KN_DEVICE_RETRY, KN_NONE, KN_NONE};
static const uint8_t PERFORMANCE_KNOB[4] = {KN_PERF_SLOT, KN_PERF_OP, KN_PERF_POS, KN_PERF_CHAR};
static const char *const PERFORMANCE_LABEL[4] = {"Slot", "Operation", "Position", "Character"};
static const char *const PLAY_LABEL[4] = {"Direction", "Groove", "Delay", "Delay time"};
static const char *const VOICE_LABEL[4] = {"MIDI channel", "Register", "Voice density", "Comp tension"};
static const char *const COMPOSER_LABEL[4] = {"Composer", "Form", "Phrase bars", "Evolution"};
static const char *const APPEARANCE_LABEL[4] = {"Theme", "Save status", "Unused", "Unused"};
static const uint8_t GLO_EXT_PORTS[7] = {
    DEVICE_CLOCK_USB, DEVICE_CLOCK_TRS, DEVICE_CLOCK_BLE,
    DEVICE_CLOCK_BOTH, DEVICE_CLOCK_USB | DEVICE_CLOCK_BLE,
    DEVICE_CLOCK_TRS | DEVICE_CLOCK_BLE, DEVICE_CLOCK_ALL
};
static const char *const GLO_EXT_NAMES[7] = {
    "USB", "TRS", "BLE", "USB + TRS", "USB + BLE", "TRS + BLE", "USB+TRS+BLE"
};
static const char *const DIR_LABEL[BL_NDIRS] = {"UP", "DOWN", "M-DWN", "PEND", "M-PEND"};
static const char *const GROOVE_LABEL[BL_NGROOVES] = {"STRAIGHT", "PULSE 8", "DRIVE 16", "BROKEN 8"};
static const char *const TIME_LABEL[7] = {"1/16", "1/8", "1/8D", "1/4", "5/16", "1/4D", "7/16"};

/* the keys: the voices' (key k is the role KEY_ROLE[k]), the degrees' (white keys 4-10), the scenes' (black keys
 * 3-7), the slots', >> and tension */
#define NVKEYS 7
static const uint8_t KEY_ROLE[NVKEYS] = {BL_ROLE_BED1, BL_ROLE_BED2, BL_ROLE_BASS, BL_ROLE_PAD, BL_ROLE_LEAD,
                                         BL_ROLE_DRONE, BL_ROLE_RHYTHM};
static const char *const ROLE_NAME[NVKEYS] = {"PLUCK 1", "PLUCK 2", "BASS", "PAD", "LEAD", "DRONE", "HEARTBEAT"};
static const uint8_t WHITE_K[16] = PLAT_WHITE_KEYS, BLACK_K[11] = PLAT_BLACK_KEYS;
#define DEGREE_W0 4                      /* C4: the white key of degree 1 */
#define SCENE_B0 3                       /* C#4: the black key of scene 1 */
#define NSCENE_KEYS 5
#define SLOT_KEY0 19                     /* C5, slot 1's key: the six slots are C5 ... F5 */
#define KEY_ADVANCE 25                   /* F#5 */
#define KEY_TENSION 26                   /* G5 */

static const char *const TPL_NAME[BL_NTPL] = {"STRING TRIO", "FULL ENSEMBLE", "AMBIENT DRIFT"};
static const char *const MODE_NAME[BL_NMODES] = {"NATURAL MINOR", "DORIAN", "PHRYGIAN", "MIXOLYDIAN", "LYDIAN",
                                                 "LYDIAN DOMINANT", "HARMONIC MINOR", "MINOR PENTATONIC", "LOCRIAN",
                                                 "MAJOR", "PMAJ", "EGYP", "HIRA", "MELM", "HMAJ", "ALTR", "GYPS",
                                                 "ROMN", "BHAI", "MARV", "PURV", "TODI", "ARAB"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
static const char *const FORM_NAME[] = {"", "CLASSIC ARC", "SLOW BUILD", "AMBIENT PLATEAU"};

/* a hold: a button's card, a voice key's select without a tap */
#define HELP_HOLD 1000u
#define DENSITY_HOLD 150u                /* MASTER: the ms a move is held before it is sent */
#define POT_STEP 41u                     /* MASTER's counts in one step (4096 / 100) */
#define POT_MOVE 60u                     /* a move: a step and a half from the place taken */
#define POT_SETTLE_MS 30u                /* at boot: the reading held within a step this long (two frames) is the
                                          * pot's place; the device's smoothing converges within a few ms */
enum { POT_BOOT, POT_REST, POT_MOVING };

enum { PERF_LOAD, PERF_STORE, PERF_RENAME };
enum { PERF_BROWSE, PERF_CONFIRM, PERF_EDIT, PERF_OVERWRITE };
enum { PERF_NO_FEEDBACK, PERF_SAVED, PERF_LOADED, PERF_ERROR };
static const char *const PERF_OP_NAME[] = {"LOAD", "SAVE", "RENAME"};
/* Backspace is a choice, never a rotary side effect. SEL applies one deletion. */
static const char PERF_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789- ";
#define PERF_BACKSPACE (sizeof PERF_CHARS - 1u)
#define PERF_SPACE (PERF_BACKSPACE - 1u)

static struct {
    uint8_t perf_slot, perf_op, perf_phase, perf_pos, perf_char, perf_feedback;
    char perf_name[PERFORMANCE_PRESET_NAME_BYTES + 1]; /* unconfirmed, RAM only */
    uint8_t view, sel;                   /* the page; the selected voice (its key) */
    uint8_t page[NVIEWS];                /* runtime-only page memory, independent for each section */
    uint32_t btn, keys, btn_used, key_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];                 /* detents in the current quick run, per knob (accel) */
    uint32_t down_t[NB];                 /* when each button went down */
    uint32_t key_t[NVKEYS];              /* when each voice key went down */
    uint8_t combo[NB];                   /* the voice key held when the button went down, plus one */
    uint32_t turn_t;                     /* the last knob turn */
    uint8_t help;                        /* a button's card is up: the button + 1 */
    uint8_t help_part;                   /* automatic continuation while the same button is held */
    uint8_t pot_state, pot_sent;         /* MASTER: a POT_; the step last sent (or of the place taken at boot) */
    uint16_t pot_ref, pot_cand;          /* the reading where the pot was last taken; where it is going (since pot_t) */
    uint32_t pot_t;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t warns;                      /* what was refused and said (the host's check) */
    uint32_t led_b, led_k, dim_k;        /* the lights as last set */
    uint8_t tpl;                         /* the template in ui_p, plus one */
    uint8_t st_ok;                       /* ui_st is the running engine's (playing) */
    uint8_t pre_ok;                      /* ui_pre is the stopped transport's (pre_sig: of which project) */
    uint32_t pre_sig;
    int8_t touched;                      /* the knob turned last (0..3), drawn on a tint for a moment */
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];                     /* what each band shows: redrawn when it changes */
} ui;

static const char *view_name(int v)
{
    return ui.page[v] == 3 && v == V_SETUP ? "GLO PERFORMANCE" :
           ui.page[v] == 2 && v == V_SETUP ? "GLO APPEARANCE" :
           ui.page[v] == 1 && v == V_SETUP ? "GLO MIDI" :
           ui.page[v] == 2 && v == V_PERFORM ? "HOME COMPOSE" :
           ui.page[v] && v == V_PERFORM ? "HOME PLAY" :
           ui.page[v] && v == V_VOICE ? "ENV VOICE" :
           ui.page[v] && v == V_DELAY ? "FX MIXER" :
           VIEW_NAME[v];
}
static int heartbeat_selected(void);
static int view_knob(int v, int i)
{
    int k = ui.page[v] == 3 && v == V_SETUP ? PERFORMANCE_KNOB[i] :
           ui.page[v] == 2 && v == V_SETUP ? APPEARANCE_KNOB[i] :
           ui.page[v] == 1 && v == V_SETUP ? MIDI_KNOB[i] :
           ui.page[v] == 2 && v == V_PERFORM ? COMPOSER_KNOB[i] :
           ui.page[v] && v == V_PERFORM ? PLAY_KNOB[i] :
           ui.page[v] && v == V_VOICE ? VOICE_KNOB[i] :
           ui.page[v] && v == V_DELAY ? MIXER_KNOB[i] :
           VIEW_KNOB[v][i];
    int pitch = ((v == V_PERFORM && !ui.page[v]) || v == V_PATTERN) && (k == KN_VROOT || k == KN_SCALE);
    if ((pitch || (v == V_VOICE && k == KN_REGISTER)) && heartbeat_selected()) {
        if (v == V_PERFORM && k == KN_VROOT) return KN_DRUM_PARTS;
        if (v == V_PERFORM && k == KN_SCALE) return KN_DRUM_GROOVE;
        return KN_NONE;
    }
    return k;
}
static const char *knob_label(int v, int i)
{
    int k = view_knob(v, i);
    if (k == KN_NONE) return "Unused";
    if (k == KN_DRUM_PARTS) return "Parts";
    if (k == KN_DRUM_GROOVE) return "Groove";
    return ui.page[v] == 3 && v == V_SETUP ? PERFORMANCE_LABEL[i] :
           ui.page[v] == 2 && v == V_SETUP ? APPEARANCE_LABEL[i] :
           ui.page[v] == 1 && v == V_SETUP ? MIDI_LABEL[i] :
           ui.page[v] == 2 && v == V_PERFORM ? COMPOSER_LABEL[i] :
           ui.page[v] && v == V_PERFORM ? PLAY_LABEL[i] :
           ui.page[v] && v == V_VOICE ? VOICE_LABEL[i] :
           ui.page[v] && v == V_DELAY ? MIXER_LABEL[i] :
           KNOB_LABEL[v][i];
}
static bl_project_t ui_p PLAT_POOL;      /* the app project's template: its voices' roles and ids, its scenes */
static bl_status_t ui_st PLAT_POOL;      /* the mirror (UIState.refresh): the running engine's snapshot */

/* the screen reader (announce.c; speak() below): a message went to it this frame, so speak() leaves what it changed
 * be */
static uint8_t sp_said;
static void msg_text(const char *a, const char *b, char *t)   /* the message as a reader gets it: "a b" */
{
    uint32_t i, k = 0;
    for (i = 0; i < 23u && a && a[i]; i++)
        t[k++] = a[i];
    if (b && b[0])
        t[k++] = ' ';
    for (i = 0; i < 23u && b && b[i]; i++)
        t[k++] = b[i];
    while (k && t[k - 1] == '.')         /* "UPDATE MODE IN 3..": the dots are for the eye */
        k--;
    t[k] = 0;
}
/* a message for a moment in the header; flags: how a screen reader gets it (0: not told, the knob strip's value
 * is, as it settles) */
static void say_as(const char *a, const char *b, uint32_t flags)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
#ifdef PLAT_HOST
    printf("  say %s / %s\n", a ? a : "", b ? b : "");
#endif
    if (flags && ann_on()) {
        msg_text(a, b, t);
        ann_say(t, 0, flags);
        sp_said = 1;
    }
}
static void say(const char *a, const char *b) { say_as(a, b, ANN_NOW); }
static void show(const char *a, const char *b) { say_as(a, b, 0); }   /* a knob's value: the reader hears it settle */
/* something did not happen: a screen reader says it whole (what follows does not cut it off) */
static void warn(const char *a, const char *b)
{
    ui.warns++;
    say_as(a, b, ANN_NOW | ANN_KEEP);
}
void ui_say(const char *a, const char *b) { say(a, b); }

static void itoa_u(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void caps(char *d, const char *s, int n)  /* s in capitals, at most n - 1 characters */
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
}

static void ratchet_text(int pct, char *t)   /* the ratchet as shown: OFF, 5% ... 50% */
{
    if (!pct) {
        memcpy(t, "OFF", 4);
        return;
    }
    itoa_u((uint32_t)pct, t);
    while (*t)
        t++;
    t[0] = '%';
    t[1] = 0;
}

/* ------------------------------------------------------------ the mirror --- */
static const bl_project_t *tpl_now(void) /* the app project's template */
{
    if (ui.tpl != proj.tpl + 1u) {
        bl_template(proj.tpl, &ui_p);
        ui.tpl = (uint8_t)(proj.tpl + 1u);
    }
    return &ui_p;
}

static int key_voice(int k)              /* voice key k's voice: its index in the template, -1 none */
{
    const bl_project_t *p = tpl_now();
    int i;
    for (i = 0; i < p->nvoices; i++)
        if (p->voice[i].role == KEY_ROLE[k])
            return i;
    return -1;
}

static int key_vid(int k)                /* voice key k's voice id code (Ruling Q), BL_VID_NONE none */
{
    int i = key_voice(k);
    return i < 0 ? BL_VID_NONE : bl_vid(tpl_now()->voice[i].id);
}

static int heartbeat_selected(void)
{
    int voice = key_voice(ui.sel);
    if (voice < 0) return 0;
    if (rt_playing()) {
        bl_vconfig_t cfg;
        if (!rt_get_voice_config(key_vid(ui.sel), NULL, &cfg)) return cfg.gen.kind == BL_GEN_HEARTBEAT;
    }
    /* Stopped: inspect the target directly, without regenerating a preview
     * for each knob-label lookup. */
    return (proj.voice_configured[voice] ? proj.voice_config[voice].gen.kind :
            tpl_now()->voice[voice].gen.kind) == BL_GEN_HEARTBEAT;
}

static void mirror(void)                 /* refresh: the running engine's snapshot (stopped: none) */
{
    ui.st_ok = (uint8_t)rt_playing();
    if (ui.st_ok)
        bl_live_status(rt_engine(), &ui_st);
}

static int vstat(int k)                  /* voice key k in the mirror: its index there, -1 none */
{
    int v = key_vid(k), i;
    for (i = 0; ui.st_ok && v != BL_VID_NONE && i < ui_st.nvoices; i++)
        if (ui_st.voice[i].vid == v)
            return i;
    return -1;
}

/* voice key k: 1 playing, 0 held back (WAITS), -1 not in the running engine (stopped: in the template, 1) */
static int v_active(int k)
{
    int i;
    if (key_voice(k) < 0)
        return -1;
    if (!ui.st_ok)
        return 1;
    return (i = vstat(k)) < 0 ? -1 : ui_st.voice[i].active;
}
static int v_locked(int k)
{
    int i = vstat(k);
    return i >= 0 && ui_st.voice[i].locked;
}

/* the degree in force at the render head's bar (Transport.currentDegreeOffset + 1), and the one the next bar brings
 * when it differs (a press binds there), 0 none; stopped: 1 and none */
static int degree_now(void) { return ui.st_ok ? bl_live_offset(rt_engine(), ui_st.bar) + 1 : 1; }
static int degree_pending(void)
{
    int32_t o;
    if (!ui.st_ok)
        return 0;
    o = bl_live_offset(rt_engine(), ui_st.bar + 1);
    return o != bl_live_offset(rt_engine(), ui_st.bar) ? o + 1 : 0;
}

/* the scene the running engine's morphs head for, a BL_SCN_; BL_SCN_NONE none */
static int scene_now(void) { return ui.st_ok ? ui_st.scene : BL_SCN_NONE; }

static int tension_left(void) { return ui.st_ok ? ui_st.tension : BL_MAX_TENSION; }   /* stopped: the budget */

/* ------------------------------------------------------------- actions --- */
static void send(const bl_action_t *x, const char *a, const char *b)   /* an engine action at the head; said */
{
    if (!rt_playing())                   /* (Play starts with no actions, as Transport.play drops them) */
        say(a, "WHEN PLAYING");
    else if (rt_action(x))
        warn(a, x->kind == BL_A_TRANSPOSE ? "SCHEDULE FULL" : "QUEUE FULL");
    else
        say(a, b);
}

static void action(int kind, int vid, int32_t arg, const char *a)
{
    bl_action_t x;
    memset(&x, 0, sizeof x);
    x.kind = (uint8_t)kind;
    x.voice = (uint8_t)vid;
    x.arg = arg;
    send(&x, a, kind == BL_A_LOCK ? (arg ? "LOCKED" : "UNLOCKED")
                : kind == BL_A_REGENERATE || kind == BL_A_BLANK ? "AT ITS LOOP" : "NEXT BAR");
}

static void undo_point(void)             /* pushUndo, and said when it took none */
{
    if (undo_push())
        warn("UNDO", "TOO BIG");
}

static void voice_tap(int k)             /* toggleMute; a voice held back: setVoiceActive(on) */
{
    int v = key_vid(k);
    if (v == BL_VID_NONE)
        return;
    mirror();
    if (v_active(k) == 0) {
        action(BL_A_VOICE_ACTIVE, v, 1, ROLE_NAME[k]);
        return;
    }
    rt_set_mute(v, !rt_muted(v));
    say(ROLE_NAME[k], rt_muted(v) ? "MUTED" : "ON");
}

static void voice_combo(int b, int k)    /* a voice key held: regenerate, blankCycle, toggleLock (a role the
                                          * template lacks: nothing, the button taken all the same) */
{
    int v = key_vid(k);
    if (v == BL_VID_NONE)
        return;
    mirror();
    if (b == B_EDIT) {
        undo_point();
        action(BL_A_REGENERATE, v, 0, ROLE_NAME[k]);
    } else if (b == B_REC) {
        action(BL_A_BLANK, v, 0, ROLE_NAME[k]);
    } else if (b == B_SEL) {
        action(BL_A_LOCK, v, !v_locked(k), ROLE_NAME[k]);
    }
}

static void reroll(void)                 /* rerollAllUnlocked: every voice the mirror shows unlocked */
{
    int i, r = 0;
    bl_action_t x;
    mirror();
    undo_point();
    if (!rt_playing()) {
        say("RE-ROLL", "WHEN PLAYING");
        return;
    }
    memset(&x, 0, sizeof x);
    x.kind = BL_A_REGENERATE;
    for (i = 0; i < ui_st.nvoices; i++)
        if (!ui_st.voice[i].locked) {
            x.voice = ui_st.voice[i].vid;
            r |= rt_action(&x);
        }
    if (r)
        warn("RE-ROLL", "QUEUE FULL");
    else
        say("RE-ROLL", "UNLOCKED VOICES");
}

static void tension(void)                /* spendTension: the pad voice, else the first */
{
    const bl_project_t *p = tpl_now();
    int i = 0, k;
    for (k = 0; k < p->nvoices; k++)
        if (p->voice[k].role == BL_ROLE_PAD) {
            i = k;
            break;
        }
    if (p->nvoices)
        action(BL_A_TENSION, bl_vid(p->voice[i].id), 0, "TENSION");
}

static void scene(int j)                 /* morphToScene(name:bars: 8): the template's scene j, if it has one */
{
    const bl_project_t *p = tpl_now();
    bl_action_t x;
    char t[12];
    if (j >= p->nscenes)
        return;
    memset(&x, 0, sizeof x);
    x.kind = BL_A_SCENE;
    x.scene = (uint8_t)bl_scn(p->scene[j].name);
    x.arg = 8;
    caps(t, p->scene[j].name, sizeof t);
    send(&x, "SCENE", t);
}

static void slot_key(int s)              /* SAVE held: saveSnapshot; else recallSnapshot */
{
    char t[8] = "SLOT ";
    t[5] = (char)('1' + s);
    t[6] = 0;
    if (ui.btn >> B_SAVE & 1u) {
        ui.btn_used |= 1u << B_SAVE;     /* (SAVE's release then saves nothing) */
        if (slot_store(s))
            warn(t, "TOO BIG");
        else
            say(t, "STORED");
        return;
    }
    if (!slot_used(s)) {
        warn(t, "EMPTY");
        return;
    }
    if (slot_recall(s))
        warn(t, "QUEUE FULL");
    else if (undo_refused())
        warn("UNDO", "TOO BIG");
    else
        say(t, rt_playing() ? "NEXT BAR" : "WHEN PLAYING");
}

static void key_down(int k)
{
    if (k < NVKEYS) {                    /* a voice: selected; its tap is on the release */
        if (key_voice(k) >= 0)
            ui.sel = (uint8_t)k;
        ui.key_t[k] = plat_ms();
        ui.key_used &= ~(1u << k);
    } else if (k == KEY_ADVANCE) {
        action(BL_A_ADVANCE, BL_VID_NONE, 0, ">>");
    } else if (k == KEY_TENSION) {
        tension();
    } else if (k >= SLOT_KEY0 && k < SLOT_KEY0 + PROJ_SLOTS) {
        slot_key(k - SLOT_KEY0);
    } else {
        int j;
        for (j = 0; j < 7; j++)          /* pressDegree(j + 1): transpose(degreeOffset: j) */
            if (WHITE_K[DEGREE_W0 + j] == k) {
                char t[10] = "DEGREE ";
                t[7] = (char)('1' + j);
                t[8] = 0;
                action(BL_A_TRANSPOSE, BL_VID_NONE, j, t);
            }
        for (j = 0; j < NSCENE_KEYS; j++)
            if (BLACK_K[SCENE_B0 + j] == k)
                scene(j);
    }
}

static void key_up(int k)
{
    if (k < NVKEYS && !(ui.key_used >> k & 1u) && plat_ms() - ui.key_t[k] < HELP_HOLD)
        voice_tap(k);
}

static int held_voice(void)              /* the voice key held (the lowest), -1 none */
{
    int k;
    for (k = 0; k < NVKEYS; k++)
        if (ui.keys >> k & 1u)
            return k;
    return -1;
}

/* ----------------------------------------------------------- the values --- */
static int performance_page(void) { return ui.view == V_SETUP && ui.page[V_SETUP] == 3; }
static void performance_cancel(void)
{
    ui.perf_phase = PERF_BROWSE;
    ui.perf_feedback = PERF_NO_FEEDBACK;
    ui.perf_pos = 0;
    ui.perf_char = PERF_SPACE;
    memset(ui.perf_name, ' ', PERFORMANCE_PRESET_NAME_BYTES);
    ui.perf_name[PERFORMANCE_PRESET_NAME_BYTES] = 0;
}
static void set_view(int v)
{
    performance_cancel();              /* all navigation discards unconfirmed name edits */
    ui.view = (uint8_t)v;
}

static void saved(int r)                 /* what a SAVE did (proj_save's, proj_stopped's): 1, nothing yet */
{
    if (r == 0)
        say("SAVE", "SAVED");
    else if (r < 0)
        warn("SAVE", "FAILED");
}

static void sel_step(int d)              /* the next voice the template has, in key order, kept within them */
{
    int k = ui.sel + d;
    while (k >= 0 && k < NVKEYS && key_voice(k) < 0)
        k += d;
    if (k >= 0 && k < NVKEYS)
        ui.sel = (uint8_t)k;
    say("VOICE", ROLE_NAME[ui.sel]);
}

static void sel_fix(void)                /* the selected voice is one the template has (a template changed) */
{
    if (key_voice(ui.sel) < 0) {
        ui.sel = 0;
        while (ui.sel < NVKEYS - 1 && key_voice(ui.sel) < 0)
            ui.sel++;
    }
}

static void tempo_to(int32_t bpm, int told)   /* the Mac's tempo: playing, from the next bar; told: a reader hears
                                               * it now (HOME + SELECT), else from the knob strip */
{
    char t[8];
    proj_set_bpm(bpm);
    itoa_u((uint32_t)proj.bpm, t);
    say_as("TEMPO", t, told ? ANN_NOW : 0);
}

/* knob acceleration by speed (AMB-1's): a few clicks are one step each however quick; a longer quick turn speeds up
 * to 8 a detent. Small ranges never accelerate; nor the host simulator's "spin" */
static int32_t accel(int role, int32_t s, int range)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (!a)
        return s;
    if (a > 1)
        dt /= a;                         /* per detent */
    ui.enc_run[role] = (uint8_t)(dt < 60u ? (ui.enc_run[role] + a > 255u ? 255u : ui.enc_run[role] + a) : a);
#ifdef PLAT_HOST
    if (plat_host_exact)
        return s;
#endif
    if (range <= 24 || ui.enc_run[role] < 4u)
        return s;
    m = dt < 12u ? 8u : dt < 25u ? 5u : dt < 45u ? 3u : 1u;
    if (range < 100 && m > 3u)
        m = 3u;
    return s * (int32_t)m;
}

/* New page controls share CORE1/2/3 targets; no page keeps a private parameter copy. */
static int voice_knob(int role, int k, int32_t e)
{
    if (k < KN_TRACK || k > KN_DECAY) return 0;
    int d = e > 0 ? 1 : -1, voice = key_voice(ui.sel);
    if (k == KN_TRACK) {
        sel_step(d);                    /* same ui.sel and missing-role skipping as SELECT */
        return 1;
    }
    if (voice < 0 || rt_loading()) {
        warn("VOICE", "NOT AVAILABLE");
        return 1;
    }
    if (k == KN_DELAY_ON || k == KN_DELAY_TIME || k == KN_FEEDBACK || k == KN_DECAY) {
        bl_delay_t target;
        if (proj_get_delay(voice, &target)) {
            warn("DELAY", "NOT AVAILABLE");
            return 1;
        }
        bl_delay_t next = target;
        if (k == KN_DELAY_ON) next.enabled = d > 0;
        if (k == KN_DELAY_TIME) next.time = (uint8_t)clamp(target.time + d, 1, 7);
        if (k == KN_FEEDBACK) next.feedback = (uint8_t)(25 * clamp(target.feedback / 25 + d, 1, 4));
        if (k == KN_DECAY) next.decay = (uint8_t)clamp(target.decay + accel(role, e, 50), 50, 100);
        if (!memcmp(&next, &target, sizeof next)) return 1;
        undo_point();
        if (proj_set_delay(voice, &next)) warn("DELAY", "NOT AVAILABLE");
        else show("MIDI DELAY", rt_playing() ? "NEXT ORIGINAL NOTE" : "SET");
        return 1;
    }
    bl_vconfig_t target;
    if (proj_get_voice_config(voice, &target)) {
        warn("CONFIG", "NOT AVAILABLE");
        return 1;
    }
    if (target.gen.kind == BL_GEN_HEARTBEAT && k == KN_GATE) return 1; /* fixed drum gate */
    if (target.gen.kind == BL_GEN_HEARTBEAT && (k == KN_DIRECTION || k == KN_GROOVE)) {
        warn("CONTROL", "ARP / MOTIF ONLY");
        return 1; /* existing N/A controls must not edit the reused drum fields */
    }
    bl_vconfig_t next = target;
    int field = -1, value = 0;
    switch (k) {
    case KN_VDENSITY: field = BL_CFG_DENSITY; value = clamp(100 - target.density_cut + accel(role, e, 100), 0, 100); break;
    case KN_GENERATOR: field = BL_CFG_GENERATOR; value = clamp(target.gen.kind + d, 0, BL_NGENS - 1); break;
    case KN_VROOT: field = BL_CFG_ROOT; value = (target.key.root + d + 12) % 12; break;
    case KN_SCALE: field = BL_CFG_SCALE; value = (target.key.scale.mode + d + BL_NMODES) % BL_NMODES; break;
    case KN_DRUM_PARTS:
        if (target.gen.kind != BL_GEN_HEARTBEAT) return 1;
        field = BL_CFG_DRUM_PARTS;
        value = clamp(bl_drum_parts(target.gen.direction) + d, 1, 4) % 4;
        break;
    case KN_DRUM_GROOVE: {
        if (target.gen.kind != BL_GEN_HEARTBEAT) return 1;
        static const uint8_t order[] = {BL_DRUM_SPARSE, BL_DRUM_STEADY, BL_DRUM_DRIVE};
        int index = target.gen.groove == BL_DRUM_SPARSE ? 0 : target.gen.groove == BL_DRUM_DRIVE ? 2 : 1;
        field = BL_CFG_DRUM_GROOVE;
        value = order[clamp(index + d, 0, 2)];
        break;
    }
    case KN_DIRECTION: field = BL_CFG_DIRECTION; value = clamp(target.gen.direction + d, 0, BL_NDIRS - 1); break;
    case KN_GROOVE: field = BL_CFG_GROOVE; value = clamp(target.gen.groove + d, 0, BL_NGROOVES - 1); break;
    case KN_RATE: field = BL_CFG_RATE; value = d > 0 ? 240 : 480; break;
    case KN_GATE: field = BL_CFG_GATE; value = clamp(target.gen.b + accel(role, e, target.gen.a), 1, target.gen.a - 1); break;
    case KN_LOOP: field = BL_CFG_LOOP; value = clamp((int32_t)(target.length / 240) + e, 1, 16); break;
    case KN_PHASE: field = BL_CFG_PHASE; value = clamp(target.phase + e, 0, bl_config_phase_count(&target) - 1); break;
    case KN_REGISTER: {
        int shift = clamp(12 * clamp(e, -11, 11), -(int)target.reg_low, 127 - target.reg_high);
        next.reg_low = (uint8_t)(target.reg_low + shift);
        next.reg_high = (uint8_t)(target.reg_high + shift);
        break;
    }
    default: return 1;
    }
    if (field >= 0 && bl_config_edit(&next, field, value)) {
        warn("CONTROL", "ARP / MOTIF ONLY");
        return 1;
    }
    if (!bl_config_diff(&target, &next)) return 1;
    undo_point();
    if (proj_set_voice_config(voice, &next)) warn("CONFIG", "NOT AVAILABLE");
    else show("VOICE CONFIG", rt_playing() ? "NEXT LOCAL LOOP" : "SET");
    return 1;
}

/* Performance page: selecting/browsing never calls a project mutation. Only
 * the explicit SEL path reaches the backend; SAVE retains its existing path. */
static void performance_name(char out[PERFORMANCE_PRESET_NAME_BYTES + 1])
{
    memset(out, ' ', PERFORMANCE_PRESET_NAME_BYTES);
    out[PERFORMANCE_PRESET_NAME_BYTES] = 0;
    if (ui.perf_phase != PERF_BROWSE) memcpy(out, ui.perf_name, sizeof ui.perf_name);
    else performance_preset_name(ui.perf_slot + 1, out);
}

static int performance_ready(void)
{
    return ui.perf_phase != PERF_BROWSE && !rt_playing() && rt_idle() &&
        (ui.perf_op == PERF_RENAME || !proj_save_pending()) &&
        performance_preset_used(ui.perf_slot + 1) >= 0;
}

static const char *performance_status(void)
{
    int used = performance_preset_used(ui.perf_slot + 1);
    if (used < 0 || ui.perf_feedback == PERF_ERROR) return "ERROR";
    if (rt_playing() && ui.perf_op == PERF_LOAD) return "STOP TO LOAD";
    if (rt_playing() && ui.perf_op == PERF_STORE) return "STOP TO SAVE";
    if (ui.perf_feedback == PERF_SAVED) return "SAVED";
    if (ui.perf_feedback == PERF_LOADED) return "LOADED";
    if (ui.perf_phase != PERF_BROWSE && !performance_ready())
        return rt_playing() ? "STOP TO RENAME" : "WAIT UNTIL IDLE";
    if (ui.perf_phase == PERF_OVERWRITE) return "OVERWRITE?";
    if (ui.perf_phase == PERF_EDIT)
        return ui.perf_char == PERF_BACKSPACE ? "SEL: BACKSPACE" : "SEL: SAVE NAME";
    if (ui.perf_phase == PERF_CONFIRM)
        return ui.perf_op == PERF_LOAD ? "SEL: LOAD" : "SEL: SAVE";
    return !used ? "EMPTY" : "SEL: PREPARE";
}

static void performance_prepare(void)
{
    int used = performance_preset_used(ui.perf_slot + 1);
    performance_cancel();
    if (used < 0) {
        ui.perf_feedback = PERF_ERROR;
        warn("PERFORMANCE", "ERROR");
        return;
    }
    if (rt_playing() && ui.perf_op != PERF_RENAME) {
        warn("PERFORMANCE", ui.perf_op == PERF_LOAD ? "STOP TO LOAD" : "STOP TO SAVE");
        return;
    }
    if (!used && ui.perf_op != PERF_STORE) {
        say("PERFORMANCE", "EMPTY");
        return;
    }
    if (used && performance_preset_name(ui.perf_slot + 1, ui.perf_name)) {
        ui.perf_feedback = PERF_ERROR;
        warn("PERFORMANCE", "ERROR");
        return;
    }
    ui.perf_phase = ui.perf_op == PERF_RENAME || (ui.perf_op == PERF_STORE && !used) ? PERF_EDIT : PERF_CONFIRM;
    if (ui.perf_phase == PERF_EDIT) {
        for (unsigned i = 0; i < PERF_BACKSPACE; i++)
            if (PERF_CHARS[i] == ui.perf_name[0]) ui.perf_char = (uint8_t)i;
    }
    say("PERFORMANCE", performance_status());
}

static void performance_confirm(void)
{
    if (ui.perf_phase == PERF_BROWSE) {
        performance_prepare();
        if (ui.perf_op != PERF_LOAD || ui.perf_phase == PERF_BROWSE)
            return;
    }
    if (ui.perf_phase == PERF_EDIT && ui.perf_char == PERF_BACKSPACE) {
        if (!ui.perf_pos) {
            say("NAME", "START OF NAME");
            return;
        }
        {
            int at = --ui.perf_pos;
            memmove(ui.perf_name + at, ui.perf_name + at + 1, PERFORMANCE_PRESET_NAME_BYTES - at - 1);
            ui.perf_name[PERFORMANCE_PRESET_NAME_BYTES - 1] = ' ';
        }
        say("NAME", "BACKSPACE");    /* retain BS selection: one deletion per deliberate SEL press */
        return;
    }
    if (!performance_ready()) {
        warn("PERFORMANCE", performance_status());
        return;
    }
    if (ui.perf_op == PERF_STORE && ui.perf_phase == PERF_CONFIRM) {
        ui.perf_phase = PERF_OVERWRITE;
        say("PERFORMANCE", "OVERWRITE?");
        return;
    }
    int error = ui.perf_op == PERF_LOAD ? performance_preset_load(ui.perf_slot + 1) :
        ui.perf_op == PERF_STORE ? performance_preset_store(ui.perf_slot + 1, ui.perf_name, PERFORMANCE_PRESET_NAME_BYTES) :
        performance_preset_rename(ui.perf_slot + 1, ui.perf_name, PERFORMANCE_PRESET_NAME_BYTES);
    if (error) {
        ui.perf_feedback = PERF_ERROR;
        warn("PERFORMANCE", "ERROR");
        return;
    }
    performance_cancel();
    if (ui.perf_op == PERF_LOAD) ui.pre_ok = 0; /* refresh the stopped preview */
    ui.perf_feedback = ui.perf_op == PERF_LOAD ? PERF_LOADED : PERF_SAVED;
    say("PERFORMANCE", performance_status());
}

static void performance_knob(int k, int32_t e)
{
    /* One choice per event, without acceleration or destructive rotary actions. */
    int d = e > 0 ? 1 : -1;
    ui.msg_until = 0;
    if (k == KN_PERF_SLOT) {
        ui.perf_slot = (uint8_t)clamp(ui.perf_slot + d, 0, PERFORMANCE_PRESET_SLOTS - 1);
        performance_cancel();
    } else if (k == KN_PERF_OP) {
        ui.perf_op = (uint8_t)clamp(ui.perf_op + d, PERF_LOAD, PERF_RENAME);
        performance_prepare();
    } else if (ui.perf_phase == PERF_EDIT) {
        ui.perf_feedback = PERF_NO_FEEDBACK;
        if (k == KN_PERF_POS) {
            ui.perf_pos = (uint8_t)clamp(ui.perf_pos + d, 0, PERFORMANCE_PRESET_NAME_BYTES);
            ui.perf_char = PERF_SPACE;
            if (ui.perf_pos < PERFORMANCE_PRESET_NAME_BYTES)
                for (unsigned i = 0; i < PERF_BACKSPACE; i++)
                    if (PERF_CHARS[i] == ui.perf_name[ui.perf_pos]) ui.perf_char = (uint8_t)i;
        } else {
            ui.perf_char = (uint8_t)((ui.perf_char + d + PERF_BACKSPACE + 1) % (PERF_BACKSPACE + 1));
            if (ui.perf_char != PERF_BACKSPACE && ui.perf_pos < PERFORMANCE_PRESET_NAME_BYTES)
                ui.perf_name[ui.perf_pos] = PERF_CHARS[ui.perf_char];
        }
    }
}

static void knob(int role, int k, int32_t e)   /* a page's knob: the values the Mac can change (shown in the
                                                * header; a reader hears the strip's value as it settles) */
{
    static const char *const LIGHTS[3] = {"OFF", "ON", "KEYS"};
    int d = e > 0 ? 1 : -1, v, i;
    char t[8];
    if (voice_knob(role, k, e)) return;
    switch (k) {
    case KN_PERF_SLOT:
    case KN_PERF_OP:
    case KN_PERF_POS:
    case KN_PERF_CHAR:
        performance_knob(k, e);
        break;
    case KN_LFO_TARGET:
    case KN_LFO_CYCLE:
    case KN_LFO_DEPTH:
    case KN_LFO_CENTER: {
        int voice = key_voice(ui.sel), target, cycle, depth, center;
        if (proj_lfo_get(voice, &target, &cycle, &depth, &center)) {
            warn("LFO", "NO VOICE");
            break;
        }
        if (k == KN_LFO_TARGET) target = clamp(target + d, 0, LFO_NDEST - 1);
        if (k == KN_LFO_CYCLE) cycle = clamp(cycle + d, 0, LFO_NCYCLES - 1);
        if (k == KN_LFO_DEPTH) depth = clamp(depth + accel(role, e, 100), 0, 100);
        if (k == KN_LFO_CENTER) center = clamp(center + accel(role, e, 100), 0, 100);
        if (proj_lfo_set(voice, target, cycle, depth, center))
            warn("LFO", "NOT AVAILABLE");
        else show("LFO", rt_playing() ? "NEXT CC WINDOW" : "SET");
        break;
    }
    case KN_MIX_VOLUME:
    case KN_MIX_PAN:
    case KN_MIX_CUTOFF:
    case KN_MIX_DECAY: {
        int voice = key_voice(ui.sel);
        int control = k - KN_MIX_VOLUME;
        int old = proj_mixer_value(voice, control);
        if (old < 0) {
            warn("MIXER", "NO VOICE");
            break;
        }
        int value = clamp(old + accel(role, e, 127), 0, 127);
        if (value != old && proj_mixer_set(voice, control, value))
            warn("MIXER", "MIDI SEND FAILED");
        else if (value != old) show("MIXER", "CC SENT");
        break;
    }
    case KN_THEME: {
        /* Display order is independent of persistent IDs (MONO=4, FIRE=5). */
        static const uint8_t order[DEVICE_NTHEMES] = {
            DEVICE_THEME_CLASSIC, DEVICE_THEME_GRAPHITE, DEVICE_THEME_AMBER,
            DEVICE_THEME_NOCTURNE, DEVICE_THEME_FIRE, DEVICE_THEME_MONO
        };
        device_settings_t settings = device_settings_get();
        int position = 0;
        while (position < DEVICE_NTHEMES - 1 && order[position] != settings.theme) position++;
        settings.theme = order[clamp(position + d, 0, DEVICE_NTHEMES - 1)];
        if (device_settings_set(settings)) warn("THEME", "REFUSED");
        else show("THEME", DEVICE_THEME_NAME[settings.theme]);
        break;
    }
    case KN_DEVICE_RETRY:
        if (device_settings_status() < 0) {
            device_settings_retry();
            show("GLOBAL SAVE", rt_playing() ? "AT STOP" : "RETRYING");
        }
        break;
    case KN_COMPOSER:
    case KN_COMP_BARS:
    case KN_COMP_EVOLUTION:
    case KN_COMP_TENSION: {
        bl_compcfg_t cfg = proj.composer;
        if (k == KN_COMPOSER) cfg.enabled = (uint8_t)(d > 0);
        if (k == KN_COMP_BARS) cfg.bars = (uint8_t)(4 * clamp(cfg.bars / 4 + d, 0, 16));
        if (k == KN_COMP_EVOLUTION) cfg.evolution = (uint8_t)clamp(cfg.evolution + d, 0, 3);
        if (k == KN_COMP_TENSION) cfg.tension = (uint8_t)clamp(cfg.tension + d, 0, 3);
        if (!memcmp(&cfg, &proj.composer, sizeof cfg)) break;
        if (proj_set_composer(&cfg)) warn("COMPOSER", "NOT AVAILABLE");
        else show("COMPOSER", rt_playing() ? "NEXT BAR" : "SET");
        break;
    }
    case KN_TEMPO: tempo_to(proj.bpm + accel(role, e, 180), 0); break;
    case KN_AUTO:
        if (proj_set_arranger(d > 0))
            warn("AUTO", "QUEUE FULL");
        else
            show("AUTO", proj.arr_on ? "ON" : "OFF");
        break;
    case KN_FORM:
        v = proj_form() == FORM_OTHER ? (d > 0 ? FORM_CLASSIC : FORM_AMBIENT) : proj_form() + d;
        v = clamp(v, FORM_CLASSIC, FORM_AMBIENT);
        if (proj_set_form(v))
            warn("FORM", "NOT AVAILABLE");
        else
            show("FORM", rt_playing() ? "NEXT SECTION" : FORM_NAME[v]);
        break;
    case KN_AMBIENT:
        if (proj_set_ambient(d > 0))
            warn("AMBIENT", "WHEN STOPPED");
        else
            show("AMBIENT", proj.arr_ambient ? "ON" : "OFF");
        break;
    case KN_CHANNEL:
        if ((i = key_voice(ui.sel)) < 0)
            break;
        if (rt_loading()) {
            warn("CHANNEL", "NOT AVAILABLE");
            break;
        }
        v = proj.channel[i];
        if (rt_playing()) {
            const bl_live_t *engine = rt_engine();
            for (int j = 0; j < engine->project.nvoices; j++)
                if (bl_vid(engine->st[j].voice.id) == key_vid(ui.sel)) v = engine->st[j].voice.midi_channel;
        }
        if (v < 0) break;
        d = clamp(v + e, 0, 15);
        if (d == v) break;
        undo_point();
        proj_set_channel(i, d);
        itoa_u(proj.channel[i] + 1u, t);
        show("CHANNEL", t);
        break;
    case KN_ROOT:
        if (proj_set_key((proj.key_root + d + 12) % 12, proj.key_mode))
            warn("ROOT", "QUEUE FULL");
        else
            show("ROOT", NOTE_NAME[proj.key_root]);
        break;
    case KN_SYNC: {
        device_settings_t settings = device_settings_get();
        settings.sync = d > 0 ? DEVICE_SYNC_EXTERNAL : DEVICE_SYNC_INTERNAL;
        if (rt_playing() || device_settings_set(settings))
            warn("SYNC", "WHEN STOPPED");
        else
            show("SYNC", device_settings_get().sync ? "EXT" : "INT");
        break;
    }
    case KN_PORT: {
        device_settings_t settings = device_settings_get();
        for (i = 0; i < 7 && GLO_EXT_PORTS[i] != settings.ext_ports; i++);
        i = clamp(i + d, 0, 6);
        settings.ext_ports = GLO_EXT_PORTS[i];
        if (device_settings_set(settings)) warn("EXT INPUT", "REFUSED");
        else show("EXT INPUT", GLO_EXT_NAMES[i]);
        break;
    }
    case KN_MIDI_OUT:
    case KN_CLOCK_OUT: {
        device_settings_t settings = device_settings_get();
        uint8_t *destination = k == KN_MIDI_OUT ? &settings.midi_out : &settings.clock_out;
        int index = clamp((int)*destination - DEVICE_MIDI_USB + d, 0, 2);
        *destination = (uint8_t)(index + DEVICE_MIDI_USB);
        if (device_settings_set(settings)) warn(k == KN_MIDI_OUT ? "MIDI OUT" : "CLOCK OUT", "STOP FIRST");
        else show(k == KN_MIDI_OUT ? "MIDI OUT" : "CLOCK OUT", DEVICE_MIDI_NAME[index]);
        break;
    }
    case KN_LIGHTS: {
        device_settings_t settings = device_settings_get();
        settings.lights = (uint8_t)clamp(settings.lights + d, 0, 2);
        if (device_settings_set(settings)) warn("LIGHTS", "REFUSED");
        else show("LIGHTS", LIGHTS[settings.lights]);
        break;
    }
    case KN_RATCHET: {                   /* 0-50 % in 5s; playing, the project's, for the next Play */
        char m[16] = "RATCHET ";
        v = proj_set_ratchet(clamp(proj.ratchet_pct + 5 * e, 0, 50));
        ratchet_text(proj.ratchet_pct, m + 8);
        if (v == 1)
            say(m, "AT NEXT PLAY");      /* (said whole: the strip's value alone would not say when) */
        else
            show("RATCHET", m + 8);
        break;
    }
    default: break;                      /* shown, not editable (spec, Pages) */
    }
}

/* ------------------------------------------------------------ buttons --- */
static void undo_action(void)            /* existing undo mechanism, now HOME + OCT- */
{
    int v = undo_pop();
    if (v == UNDO_NONE)
        warn("UNDO", "NOTHING TO UNDO");
    else if (v == UNDO_FULL)
        warn("UNDO", "QUEUE FULL");
    else
        say("UNDO", rt_playing() ? "NEXT BAR" : 0);
}

static void button(int b)                /* a button's job, on its release (PLAY: its press) */
{
    int v, k = ui.combo[b] - 1;
    for (v = 0; v < NVIEWS; v++)         /* a page's button: its page */
        if (VIEW_BTN[v] == b) {
            if (plat_ms() - ui.down_t[b] >= HELP_HOLD) return;
            if (ui.view == v) ui.page[v] = (uint8_t)((ui.page[v] + 1u) % VIEW_PAGES[v]);
            set_view(v);
            ui.touched = -1;
            ui.touch_until = 0;
            return;
        }
    if (k >= 0) {                        /* a voice key was held at the press */
        voice_combo(b, k);
        return;
    }
    switch (b) {
    case B_PLAY:
        if (rt_playing()) {
            rt_stop();
            say("STOPPED", 0);
            saved(proj_stopped());       /* (what changed meanwhile reaches the transport; a SAVE waiting is written
                                          * on a later frame, once the Stop and the offs are out) */
        } else {
            proj_ready();
            rt_play();
            say("PLAYING", TPL_NAME[proj.tpl]);
        }
        break;
    case B_REC: action(BL_A_BPART, BL_VID_NONE, 0, "B PART"); break;
    case B_EDIT: reroll(); break;
    case B_SEL:
        if (performance_page()) performance_confirm();
        else if (ui.view == V_VOICE)
            voice_combo(B_SEL, ui.sel);
        break;
    case B_SAVE:
        v = proj_save();
        if (v == 1)
            say("SAVE", "AT THE STOP");
        saved(v);
        break;
    case B_OCTDN:
        if (plat_ms() - ui.down_t[B_OCTDN] < HELP_HOLD) reroll();
        break;
    case B_OCTUP:
        if (proj_dice())
            warn("DICE", "QUEUE FULL");
        else if (undo_refused())
            warn("UNDO", "TOO BIG");
        else
            say("DICE", rt_playing() ? "NEXT BAR" : "NEW SEED");
        break;
    default: break;
    }
}

/* HELP CARDS (AMB-1's): hold one button alone for HELP_HOLD and a card says what it does. A key, a turn or another
 * button puts it away; letting go after its card does nothing else. PLAY acts on its press: it has no card (Ruling
 * AC); nor has a button pressed with a voice key held (its job is that voice's, whenever it is let go) */
static int help_count(int b);
static void help_tick(uint32_t btn, uint32_t keys)
{
    uint32_t now = plat_ms();
    int i;
    if (ui.help) {
        uint32_t m = 1u << (ui.help - 1);
        if (btn != m || keys || (int32_t)(ui.turn_t - ui.down_t[ui.help - 1]) > 0)
            ui.help = 0;
        else ui.help_part = (uint8_t)(((now - ui.down_t[ui.help - 1] - HELP_HOLD) / 5000u) %
                                     (unsigned)help_count(ui.help - 1));
        return;
    }
    if (!btn || (btn & (btn - 1u)) || keys)
        return;
    for (i = 0; i < NB; i++)
        if (btn == 1u << i && i != B_PLAY && !ui.combo[i] && !(ui.btn_used & btn) && now - ui.down_t[i] >= HELP_HOLD
            && (int32_t)(ui.turn_t - ui.down_t[i]) <= 0) {   /* (a button held for a voice is no card) */
            ui.help = (uint8_t)(i + 1);
            ui.help_part = 0;
            ui.btn_used |= btn;          /* its release now does nothing */
        }
}

/* a button's card (help_tick; drawn in the page's place, screen.c): its name, then what it does (AMB-1's). A line
 * starting with a space goes on from the one before (a screen reader joins them: sp_line) */
#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
#include "ui_help.h"

/* MASTER's reading (0..4096) as a density step, 0-100 %, rounded: an end zone at each end (0-20: 0 %; from 4076:
 * 100 %), as the device's smoothed pot tops out at about 4092 (Ruling AB) */
static uint32_t pot_step(uint32_t m)
{
    uint32_t s = (m * 100u + 2048u) / 4096u;
    return s > 100u ? 100u : s;
}

static uint32_t pot_dist(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

/* MASTER: density in steps of 1 %, with hysteresis on the reading itself: the place the pot was last taken at
 * (pot_ref) is left only by a move of POT_MOVE counts or more, or into an end step (0 %, 100 %: always reachable), so
 * a pot resting on a step's edge never flips the density. A move is sent once the reading has held within a step of
 * where it went DENSITY_HOLD ms (setDensity: playing, at the next bar). At boot the saved density wins (Ruling AD):
 * the platform reports the pot's middle until its smoothing has caught up, so the first reading that holds within a
 * step for POT_SETTLE_MS is taken as the pot's place, not sent; a move from there on is the pot's */
static void pot(void)
{
    uint32_t m = plat_master(), now = plat_ms(), s = pot_step(m);
    char t[8];
    if (ui.pot_state == POT_BOOT) {      /* still converging: wait until it holds, then take the place */
        if (pot_dist(m, ui.pot_cand) > POT_STEP) {
            ui.pot_cand = (uint16_t)m;
            ui.pot_t = now;
        } else if (now - ui.pot_t >= POT_SETTLE_MS) {
            ui.pot_state = POT_REST;
            ui.pot_ref = (uint16_t)m;
            ui.pot_sent = (uint8_t)s;
        }
        return;
    }
    if (ui.pot_state == POT_REST) {      /* a move away from the place taken: from here */
        if (s == ui.pot_sent || (pot_dist(m, ui.pot_ref) < POT_MOVE && s != 0u && s != 100u))
            return;
        ui.pot_state = POT_MOVING;
        ui.pot_cand = (uint16_t)m;
        ui.pot_t = now;
        return;
    }
    if (pot_dist(m, ui.pot_cand) > POT_STEP) {   /* still moving */
        ui.pot_cand = (uint16_t)m;
        ui.pot_t = now;
        return;
    }
    if (now - ui.pot_t < DENSITY_HOLD)
        return;
    ui.pot_state = POT_REST;             /* it holds: the new place */
    ui.pot_ref = (uint16_t)m;
    if (s == ui.pot_sent)
        return;                          /* (back where it was) */
    ui.pot_sent = (uint8_t)s;
    itoa_u(s, t);
    if (proj_set_density((int)s))
        warn("DENSITY", "QUEUE FULL");
    else
        say("DENSITY", t);
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    const uint32_t shift_undo = 1u << B_HOME | 1u << B_OCTDN;
    int32_t e;
    ch = btn ^ ui.btn;
    int was_undo_chord = (ui.btn & shift_undo) == shift_undo;
    ui.btn = btn;
    /* Clear new presses together, before chord detection, so button index order
     * cannot undo a consumption flag or let a chord's release navigate/mutate. */
    ui.btn_used &= ~(ch & btn);
    if ((btn & shift_undo) == shift_undo) {
        ui.btn_used |= shift_undo;
        ui.help = 0;
        /* Preserve the OCT-/OCT+ update-mode combination; held frames do not
         * repeat undo. Re-pressing either chord button is a distinct press. */
        if (!was_undo_chord && !(btn & (1u << B_OCTUP))) undo_action();
    }
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            int k = i == B_EDIT || i == B_REC || i == B_SEL ? held_voice() : -1;
            ui.down_t[i] = plat_ms();
            ui.combo[i] = (uint8_t)(k + 1);  /* a voice key held: the button is for that voice */
            if (k >= 0)
                ui.key_used |= 1u << k;  /* (its release is then no tap) */
            if ((btn & (1u << B_OCTDN | 1u << B_OCTUP)) == (1u << B_OCTDN | 1u << B_OCTUP))
                ui.btn_used |= 1u << B_OCTDN | 1u << B_OCTUP;   /* both: fm1-base's update mode, neither's job */
            if (i == B_PLAY)             /* the transport on the press */
                button((int)i);
        } else if (!(ui.btn_used & m) && i != B_PLAY) {
            button((int)i);
        }
    }
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++)
        if (ch >> i & 1u) {
            if (keys >> i & 1u)
                key_down((int)i);
            else
                key_up((int)i);
        }
    if ((e = plat_enc(EN_SELECT)) != 0) {   /* SELECT: the voice; with HOME held, the tempo */
        if (btn >> B_HOME & 1u) {
            tempo_to(proj.bpm + accel(EN_SELECT, e, 180), 1);
            ui.btn_used |= 1u << B_HOME;
        } else {
            sel_step(e > 0 ? 1 : -1);
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        if (ui.view == V_PERFORM && ui.page[V_PERFORM] == 2 && proj.composer.enabled) {
            const bl_composer_t *c = &rt_engine()->composer;
            int previous = rt_playing() ? (c->pending_stage ? c->pending_stage - 1 : c->section) : proj_composer_start();
            /* Raw detent count only: no acceleration and no cyclic wrap. */
            int section = clamp(previous + e, 0, 6);
            if (section != previous) {
                if (proj_select_composer_stage(section)) warn("COMPOSER", "ENABLE FIRST / BUSY");
                else show(BL_COMP_NAME[bl_comp_phase(section)], !rt_playing() ? "START SECTION" :
                    c->pending_stage ? "NEXT BAR" : "CURRENT SECTION");
            }
        } else if (ui.view != V_PERFORM && !(ui.view == V_PATTERN && heartbeat_selected())) {
            int m = (proj.key_mode + (e > 0 ? 1 : -1) + BL_NMODES) % BL_NMODES;
            if (proj_set_key(proj.key_root, m)) warn("MODE", "QUEUE FULL");
            else say("MODE", MODE_NAME[m]);
        }
        if (ui.view != V_PERFORM || ui.page[V_PERFORM] == 2) ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {   /* PRESETS: clamp; endpoints must not reseed */
        int tpl = clamp(proj.tpl + (e > 0 ? 1 : -1), 0, BL_NTPL - 1);
        if (tpl != proj.tpl) {
            proj_set_template(tpl);
            sel_fix();
            say(TPL_NAME[proj.tpl], rt_playing() ? "NEXT BAR" : 0);
        }
        ui.turn_t = plat_ms();
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            knob(EN_K1 + (int)i, view_knob(ui.view, (int)i), e);
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 1200u;
            ui.turn_t = plat_ms();
        }
    pot();
    help_tick(btn, keys);
}

/* ------------------------------------------------------------- lights --- */
/* the page's button, PLAY while playing, SAVE blinking while a SAVE waits for the stop; the keys (lights 0: none):
 * a voice lit while it plays, blinking while it WAITS, dark muted or not in the template; the degree in force lit, a
 * pending one blinking; the template's scenes dim, the one the morphs head for lit; the stored slots lit; G5 lit
 * while some tension is left. PLAY lights green (plat_leds' way) */
static void leds(void)
{
    uint32_t b = 1u << VIEW_BTN[ui.view], k = 0, dk = 0, blink = plat_ms() >> 8 & 1u, play = 1u << B_PLAY;
    uint32_t lb, db;
    const bl_project_t *p = tpl_now();
    int i, d, sc;
    uint8_t lights = device_settings_get().lights;
    if (rt_playing())
        b |= play;
    if (proj_save_pending() && blink)
        b |= 1u << B_SAVE;
    int load_ready = ui.perf_op == PERF_LOAD &&
        ui.perf_phase == PERF_BROWSE &&
        ui.perf_feedback != PERF_LOADED &&
        !rt_playing() && rt_idle() && !proj_save_pending() &&
        performance_preset_used(ui.perf_slot + 1) == 1;
    int confirm = performance_page() && !ui.help &&
        (performance_ready() || load_ready);
    if (confirm && ((plat_ms() >> (ui.perf_phase == PERF_OVERWRITE ? 6 : 8)) & 1u))
        b |= 1u << B_SEL;
    for (i = 0; lights && i < NVKEYS; i++) {
        int a = v_active(i), v = key_vid(i);
        if (v == BL_VID_NONE || rt_muted(v))
            continue;
        if (a == 1 || (a == 0 && blink))
            k |= 1u << i;
    }
    if (lights) {
        d = degree_now();
        if (d >= 1 && d <= 7)
            k |= 1u << WHITE_K[DEGREE_W0 + d - 1];
        d = degree_pending();
        if (d >= 1 && d <= 7 && blink)
            k |= 1u << WHITE_K[DEGREE_W0 + d - 1];
        sc = scene_now();
        for (i = 0; i < NSCENE_KEYS && i < p->nscenes; i++) {
            if (sc != BL_SCN_NONE && bl_scn(p->scene[i].name) == sc)
                k |= 1u << BLACK_K[SCENE_B0 + i];
            else
                dk |= 1u << BLACK_K[SCENE_B0 + i];
        }
        for (i = 0; i < PROJ_SLOTS; i++)
            if (slot_used(i))
                k |= 1u << (SLOT_KEY0 + i);
        if (tension_left() > 0)
            k |= 1u << KEY_TENSION;
    }
    ui.led_b = b;
    ui.led_k = k;
    ui.dim_k = dk;
    lb = (b & ~play) | (b & play ? LED_PLAY_GREEN : 0u);
    db = lights == 1 ? ((1u << NB) - 1u) & ~b & ~play : 0u;   /* the unlit buttons glow (lights 1) */
    if (confirm) db &= ~(1u << B_SEL); /* blinking confirmation must have a dark half even in glow mode */
    plat_lights(lb, k, db, dk, lights ? 2 : 0);
}

/* ------------------------------------------------------------ the loop --- */
/* the screen and the screen reader (screen.c, after this file in the unity build) */
static void screen_init(void);
static void lanes(void);
static void draw_header(void);
static void draw_main(void);
static void draw_knobs(void);
static void speak(void);

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    performance_cancel();
    ui.pot_state = POT_BOOT;             /* (pot() takes its place once the reading holds) */
    ui.pot_cand = (uint16_t)plat_master();
    ui.pot_t = plat_ms();
    sel_fix();
    screen_init();
}

void ui_frame(void)
{
    input();
    if (!rt_playing())
        saved(proj_stopped());           /* a stop of the transport's own (MIDI Stop, the arranger's ending) */
    sel_fix();
    mirror();
    lanes();
    leds();
    draw_header();
    draw_main();
    draw_knobs();
    speak();
    ann_tick(plat_ms());
    ui.frame++;
}

void ui_input_only(void) { input(); }
