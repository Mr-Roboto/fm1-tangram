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
 *   SEL            lock (on the Voice page) SAVE  saves             OCT- / OCT+   undo / Dice (a new seed)
 *   HOME ENV ARP LFO SEQ FX GLO   the pages: PERFORM, VOICE, PATTERN, CC, ARRANGER, DELAY, SETUP
 *   MASTER         density                 SELECT the voice (HOME held: tempo)
 *   ALGORITHM      the mode                PRESETS the template
 *   KNOB 1-4       the page's four values: those the Mac can change; the rest are shown, their knobs do nothing
 *
 * Every action takes the Mac's way (UIState, AppServices): the engine's action at the render head (rt_action), the
 * transport's mute, the project's change (proj_*), an undo point first where UIState pushes one. What is refused
 * says so (the engine's queue or the transposition schedule full, a state too big for a slot or an undo point). */

/* the pages, a button each */
enum { V_PERFORM, V_VOICE, V_PATTERN, V_CC, V_ARRANGER, V_DELAY, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Perform", "Voice", "Pattern", "CC", "Arranger", "Delay", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_ARP, B_LFO, B_SEQ, B_FX, B_GLO};
/* the four knobs of each page (KN_); the values the Mac cannot change (KNS_) are shown, not edited */
enum {
    KN_NONE, KN_TEMPO, KN_AUTO, KN_FORM, KN_AMBIENT, KN_CHANNEL, KN_ROOT, KN_SYNC, KN_PORT, KN_LIGHTS,
    KNS_LOOP, KNS_REGISTER, KNS_GEN, KNS_TARGET, KNS_CYCLE, KNS_DEPTH, KNS_SCENE, KNS_PHASE, KNS_LENGTH, KNS_DROP,
    KNS_REPRISE
};
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {KN_TEMPO, KN_AUTO, KN_FORM, KN_AMBIENT},
    {KN_CHANNEL, KNS_LOOP, KNS_REGISTER, KNS_GEN},
    {KN_NONE, KN_NONE, KN_NONE, KN_NONE},
    {KNS_TARGET, KNS_CYCLE, KNS_DEPTH, KNS_SCENE},
    {KNS_PHASE, KNS_LENGTH, KNS_DROP, KNS_REPRISE},
    {KN_NONE, KN_NONE, KN_NONE, KN_NONE},
    {KN_ROOT, KN_SYNC, KN_PORT, KN_LIGHTS},
};

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

static const char *const TPL_NAME[BL_NTPL] = {"DEMO TRIO", "FULL ENSEMBLE", "AMBIENT DRIFT"};
static const char *const MODE_NAME[BL_NMODES] = {"NATURAL MINOR", "DORIAN", "PHRYGIAN", "MIXOLYDIAN", "LYDIAN",
                                                 "LYDIAN DOMINANT", "HARMONIC MINOR", "MINOR PENTATONIC", "LOCRIAN",
                                                 "MAJOR"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
static const char *const FORM_NAME[] = {"", "CLASSIC ARC", "SLOW BUILD", "AMBIENT PLATEAU"};

/* the screen's colours, until the pages come: the Mac's dark panel, its tangerine accent */
#define K_BG RGB(18, 18, 22)
#define K_PANEL RGB(36, 36, 44)
#define K_TEXT RGB(232, 232, 236)
#define K_DIM RGB(120, 120, 130)
#define K_ACC RGB(240, 140, 50)

/* a hold: a button's card, a voice key's select without a tap */
#define HELP_HOLD 1000u
#define DENSITY_HOLD 150u                /* MASTER: the ms a move is held before it is sent */
#define POT_STEP 41u                     /* MASTER's counts in one step (4096 / 100) */
#define POT_MOVE 60u                     /* a move: a step and a half from the place taken */
#define POT_SETTLE_MS 30u                /* at boot: the reading held within a step this long (two frames) is the
                                          * pot's place; the device's smoothing converges within a few ms */
enum { POT_BOOT, POT_REST, POT_MOVING };

static struct {
    uint8_t view, sel;                   /* the page; the selected voice (its key) */
    uint32_t btn, keys, btn_used, key_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];                 /* detents in the current quick run, per knob (accel) */
    uint32_t down_t[NB];                 /* when each button went down */
    uint32_t key_t[NVKEYS];              /* when each voice key went down */
    uint8_t combo[NB];                   /* the voice key held when the button went down, plus one */
    uint32_t turn_t;                     /* the last knob turn */
    uint8_t help;                        /* a button's card is up: the button + 1 */
    uint8_t pot_state, pot_sent;         /* MASTER: a POT_; the step last sent (or of the place taken at boot) */
    uint16_t pot_ref, pot_cand;          /* the reading where the pot was last taken; where it is going (since pot_t) */
    uint32_t pot_t;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t warns;                      /* what was refused and said (the host's check) */
    uint32_t led_b, led_k, dim_k;        /* the lights as last set */
    uint8_t tpl;                         /* the template in ui_p, plus one */
    uint8_t st_ok;                       /* ui_st is the running engine's (playing) */
    uint32_t frame;
    uint32_t sig[2];                     /* what each band shows: redrawn when it changes */
} ui;
static bl_project_t ui_p PLAT_POOL;      /* the app project's template: its voices' roles and ids, its scenes */
static bl_status_t ui_st PLAT_POOL;      /* the mirror (UIState.refresh): the running engine's snapshot */

/* a message for a moment in the header */
static void say(const char *a, const char *b)
{
    uint32_t i;
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
}
/* something did not happen */
static void warn(const char *a, const char *b)
{
    ui.warns++;
    say(a, b);
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
static void set_view(int v) { ui.view = (uint8_t)v; }

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

static void tempo_to(int32_t bpm)        /* the Mac's tempo: playing, from the next bar */
{
    char t[8];
    proj_set_bpm(bpm);
    itoa_u((uint32_t)proj.bpm, t);
    say("TEMPO", t);
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

static void knob(int role, int k, int32_t e)   /* a page's knob: the values the Mac can change */
{
    static const uint8_t PORTS[3] = {1u << PORT_USB, 1u << PORT_TRS, 1u << PORT_USB | 1u << PORT_TRS};
    static const char *const PORT_NAME[3] = {"USB", "TRS", "USB + TRS"};
    static const char *const LIGHTS[3] = {"OFF", "ON", "KEYS"};
    int d = e > 0 ? 1 : -1, v, i;
    char t[8];
    switch (k) {
    case KN_TEMPO: tempo_to(proj.bpm + accel(role, e, 180)); break;
    case KN_AUTO:
        if (proj_set_arranger(d > 0))
            warn("AUTO", "QUEUE FULL");
        else
            say("AUTO", proj.arr_on ? "ON" : "OFF");
        break;
    case KN_FORM:
        v = proj_form() == FORM_OTHER ? (d > 0 ? FORM_CLASSIC : FORM_AMBIENT) : proj_form() + d;
        v = clamp(v, FORM_CLASSIC, FORM_AMBIENT);
        if (proj_set_form(v))
            warn("FORM", "WHEN STOPPED");
        else
            say("FORM", FORM_NAME[v]);
        break;
    case KN_AMBIENT:
        if (proj_set_ambient(d > 0))
            warn("AMBIENT", "WHEN STOPPED");
        else
            say("AMBIENT", proj.arr_ambient ? "ON" : "OFF");
        break;
    case KN_CHANNEL:
        if ((i = key_voice(ui.sel)) < 0)
            break;
        proj_set_channel(i, clamp(proj.channel[i] + e, 0, 15));
        itoa_u(proj.channel[i] + 1u, t);
        say("CHANNEL", t);
        break;
    case KN_ROOT:
        if (proj_set_key((proj.key_root + d + 12) % 12, proj.key_mode))
            warn("ROOT", "QUEUE FULL");
        else
            say("ROOT", NOTE_NAME[proj.key_root]);
        break;
    case KN_SYNC:
        if (proj_set_sync(d > 0))
            warn("SYNC", "WHEN STOPPED");
        else
            say("SYNC", proj.sync ? "EXT" : "INT");
        break;
    case KN_PORT:
        for (i = 0; i < 2 && PORTS[i] != proj.ext_ports; i++)
            ;
        i = clamp(i + d, 0, 2);
        proj_set_ext_ports(PORTS[i]);
        say("EXT INPUT", PORT_NAME[i]);
        break;
    case KN_LIGHTS:
        proj.lights = (uint8_t)clamp(proj.lights + d, 0, 2);
        say("LIGHTS", LIGHTS[proj.lights]);
        break;
    default: break;                      /* shown, not editable (spec, Pages) */
    }
}

/* ------------------------------------------------------------ buttons --- */
static void button(int b)                /* a button's job, on its release (PLAY: its press) */
{
    int v, k = ui.combo[b] - 1;
    for (v = 0; v < NVIEWS; v++)         /* a page's button: its page */
        if (VIEW_BTN[v] == b) {
            set_view(v);
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
            saved(proj_stopped());       /* (what changed meanwhile reaches the transport, a SAVE waiting is written) */
        } else {
            proj_ready();
            rt_play();
            say("PLAYING", TPL_NAME[proj.tpl]);
        }
        break;
    case B_REC: action(BL_A_BPART, BL_VID_NONE, 0, "B PART"); break;
    case B_EDIT: reroll(); break;
    case B_SEL:
        if (ui.view == V_VOICE)
            voice_combo(B_SEL, ui.sel);
        break;
    case B_SAVE:
        v = proj_save();
        if (v == 1)
            say("SAVE", "AT THE STOP");
        saved(v);
        break;
    case B_OCTDN:
        v = undo_pop();
        if (v == UNDO_NONE)
            warn("UNDO", "NOTHING TO UNDO");
        else if (v == UNDO_FULL)
            warn("UNDO", "QUEUE FULL");
        else
            say("UNDO", rt_playing() ? "NEXT BAR" : 0);
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
static void help_tick(uint32_t btn, uint32_t keys)
{
    uint32_t now = plat_ms();
    int i;
    if (ui.help) {
        uint32_t m = 1u << (ui.help - 1);
        if (btn != m || keys || (int32_t)(ui.turn_t - ui.down_t[ui.help - 1]) > 0)
            ui.help = 0;
        return;
    }
    if (!btn || (btn & (btn - 1u)) || keys)
        return;
    for (i = 0; i < NB; i++)
        if (btn == 1u << i && i != B_PLAY && !ui.combo[i] && !(ui.btn_used & btn) && now - ui.down_t[i] >= HELP_HOLD
            && (int32_t)(ui.turn_t - ui.down_t[i]) <= 0) {   /* (a button held for a voice is no card) */
            ui.help = (uint8_t)(i + 1);
            ui.btn_used |= btn;          /* its release now does nothing */
        }
}

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
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            int k = i == B_EDIT || i == B_REC || i == B_SEL ? held_voice() : -1;
            ui.btn_used &= ~m;
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
            tempo_to(proj.bpm + accel(EN_SELECT, e, 180));
            ui.btn_used |= 1u << B_HOME;
        } else {
            sel_step(e > 0 ? 1 : -1);
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {  /* ALGORITHM: the mode, round (applyLiveMusicalChanges) */
        int m = (proj.key_mode + (e > 0 ? 1 : -1) + BL_NMODES) % BL_NMODES;
        if (proj_set_key(proj.key_root, m))
            warn("MODE", "QUEUE FULL");
        else
            say("MODE", MODE_NAME[m]);
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {   /* PRESETS: the template, round (applyTemplate) */
        proj_set_template((proj.tpl + (e > 0 ? 1 : -1) + BL_NTPL) % BL_NTPL);
        sel_fix();
        say(TPL_NAME[proj.tpl], rt_playing() ? "NEXT BAR" : 0);
        ui.turn_t = plat_ms();
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            knob(EN_K1 + (int)i, VIEW_KNOB[ui.view][i], e);
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
    if (rt_playing())
        b |= play;
    if (proj_save_pending() && blink)
        b |= 1u << B_SAVE;
    for (i = 0; proj.lights && i < NVKEYS; i++) {
        int a = v_active(i), v = key_vid(i);
        if (v == BL_VID_NONE || rt_muted(v))
            continue;
        if (a == 1 || (a == 0 && blink))
            k |= 1u << i;
    }
    if (proj.lights) {
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
    db = proj.lights == 1 ? ((1u << NB) - 1u) & ~b & ~play : 0u;   /* the unlit buttons glow (lights 1) */
    plat_lights(lb, k, db, dk, proj.lights ? 2 : 0);
}

/* ------------------------------------------------------------- screen --- */
/* until the pages (Task 16): the header (the message, else the template, the tempo, the transport) and, in the
 * page's place, its name or a button's card */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 144

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

static void draw_header(void)
{
    char t[16];
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(2166136261u, proj.tpl | (uint32_t)rt_playing() << 4 | (uint32_t)msg << 5 | proj.sync << 6),
                      (uint32_t)proj.bpm | ui.msg_until << 8);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], K_ACC);
    } else {
        if (proj.sync) {
            t[0] = 'E', t[1] = 'X', t[2] = 'T', t[3] = 0;
        } else {
            itoa_u((uint32_t)proj.bpm, t);
        }
        cv_text(10, 6, &FONT_B, TPL_NAME[proj.tpl], K_TEXT);
        cv_text(232 - text_w(&FONT_S, t), 6, &FONT_S, t, rt_playing() ? K_TEXT : K_DIM);
    }
    cv_blit(0, 0);
}

/* a button's card (help_tick): its name, then what it does. A line starting with a space goes on from the one
 * before */
#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("PERFORM"); L("TEMPO, AUTO, FORM, AMBIENT"); L("+ SELECT: TEMPO"); break;
    case B_ENV: L("THE VOICE"); L("CHANNEL, LOOP, REGISTER,"); L(" GENERATOR"); break;
    case B_ARP: L("THE PATTERN"); L("ITS STEPS, GENERATION"); L(" AND LEGITIMIZATION"); break;
    case B_LFO: L("CC AND MODULATION"); L("TARGET, CYCLE, DEPTH,"); L(" SCENE"); break;
    case B_SEQ: L("THE ARRANGER"); L("PHASE, LENGTH, BED DROP,"); L(" REPRISE"); break;
    case B_FX: L("DELAY TIMES"); L("FOR YOUR EFFECTS"); L(" AT THE TEMPO"); break;
    case B_GLO: L("SETUP"); L("ROOT, SYNC, EXT INPUT,"); L(" LIGHTS"); break;
    case B_EDIT: L("RE-ROLL EVERY"); L(" UNLOCKED VOICE"); L("WITH A VOICE KEY:"); L(" REGENERATE IT"); break;
    case B_REC: L("THE B PART"); L("WITH A VOICE KEY:"); L(" A BLANK CYCLE"); break;
    case B_SEL: L("WITH A VOICE KEY,"); L(" OR ON THE VOICE PAGE:"); L("LOCK OR UNLOCK IT"); break;
    case B_SAVE: L("SAVES THE PROJECT"); L(" AND THE SIX SLOTS"); L("+ A SLOT KEY: STORE"); break;
    case B_OCTDN: L("UNDO"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("DICE: A NEW SEED"); L(" AT THE NEXT BAR"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    default: break;
    }
#undef L
    return n;
}

static void draw_main(void)
{
    char t[16];
    const char *l[HELP_LINES];
    int i, n;
    uint32_t h = ui.help ? hash(0x48454C50u, ui.help) : hash(2166136261u, ui.view);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    if (ui.help) {
        n = help_lines(ui.help - 1, l);
        cv_rect(16, 14, 208, 124, K_PANEL);
        cv_rect(16, 14, 208, 26, K_TEXT);
        text_c(120, 18, &FONT_B, BTN_LABEL[ui.help - 1], K_BG);
        for (i = 0; i < n; i++)
            text_c(120, 56 + i * 20, &FONT_S, l[i] + (l[i][0] == ' '), K_TEXT);
    } else {                             /* the page's name, until the pages are drawn */
        caps(t, VIEW_NAME[ui.view], sizeof t);
        text_c(120, 56, &FONT_L, t, K_ACC);
    }
    cv_blit(0, MAIN_Y);
}

/* ------------------------------------------------------------ the loop --- */
void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.pot_state = POT_BOOT;             /* (pot() takes its place once the reading holds) */
    ui.pot_cand = (uint16_t)plat_master();
    ui.pot_t = plat_ms();
    sel_fix();
    lcd_fill(0, 0, 240, 240, K_BG);
}

void ui_frame(void)
{
    input();
    if (!rt_playing())
        saved(proj_stopped());           /* a stop of the transport's own (MIDI Stop, the arranger's ending) */
    sel_fix();
    mirror();
    leds();
    draw_header();
    if (ui.frame % 3u == 0)
        draw_main();
    ui.frame++;
}

void ui_input_only(void) { input(); }
