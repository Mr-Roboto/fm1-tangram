/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included after ui and button labels. Four lines per card, no heap/state
 * beyond ui.help_part. First card follows the selected subpage. */
typedef struct { const char *line[4]; } ui_help_card_t;
static const ui_help_card_t HELP_HOME_MORE[] = {
    {{"SELECT: VOICE", "HOME + SELECT: TEMPO", "PRESETS: TEMPLATE", "ALGORITHM: MODE OFF HOME"}},
    {{"MASTER: GLOBAL DENSITY", "VOICE DENSITY: ENV ROUTING", "MASTER x VOICE x COMPOSER", "0%: TIME KEEPS RUNNING"}},
    {{"F3-B3: VOICE KEYS", "TAP: MUTE / UNMUTE", "WAITS: TAP TO ACTIVATE", "HOLD: SELECT, NO MUTE"}},
    {{"C4-B4 WHITE: HARMONY", "BLACK C#4-A#4: SCENES", "F#5 ADVANCE; G5 TENSION", "THESE ACTIONS: NEXT BAR"}},
    {{"PLAY: START / STOP PRESS", "C5-F5: RECALL SLOTS 1-6", "SAVE + SLOT: CAPTURE", "SAVE AFTER STOP: PERSIST"}},
    {{"COMPOSE ALGORITHM: STAGE", "ONE CLICK / STAGE; CLAMPED", "PLAY: PENDING, NEXT BAR", "STOPPED: START FOR PLAY"}},
    {{"COMPOSER RESPECTS LOCK", "VOICE EDITS TAKE OWNERSHIP", "RETURN: CAPTURED THEME", "START STAGE NOT SAVED"}},
    {{"MOVING: 15-STEP VARIATION", "ACTIVE: 13 STEPS / REGISTER", "B RATE: ACTIVE + LIFT/PEAK", "ARP/MOTIF AT LOCAL LOOP"}}
};
static const ui_help_card_t HELP_ENV_MORE[] = {
    {{"TAP ENV: TIMING / ROUTING", "SELECT: CHOOSE VOICE", "SEL: LOCK / UNLOCK", "MANUAL EDITS ALLOW LOCK"}},
    {{"EDITS: NEXT LOCAL LOOP", "RATE / GATE: ARP OR MOTIF", "PHASE: PITCH ORDER OFFSET", "VOICE LOOPS STAY SEPARATE"}}
};
static const ui_help_card_t HELP_ARP_MORE[] = {
    {{"ARP: REPEATING SOURCE", "MOTIF: STABLE, EVOLVING", "RATE / GATE ON ENV TIMING", "DIRECTION / GROOVE: ARP/MOTIF"}},
    {{"FILLED: PASSES FILTERS", "HOLLOW / DASHED: SUPPRESSED", "RED: LAST ORIGINAL NOTE", "GREY: TIME, EVEN AT 0%"}}
};
static const ui_help_card_t HELP_LFO_MORE[] = {
    {{"BLACK C#4-A#4: SCENES", "ONLY TEMPLATE SCENES WORK", "EIGHT-BAR CONTROLLER MORPH", "SELECT: CHOOSE VOICE"}}
};
static const ui_help_card_t HELP_SEQ_MORE[] = {
    {{"EDIT AT EACH LOCAL LOOP", "PHASE: PITCH, NOT CLOCK", "DIRECTION/GROOVE: ARP/MOTIF", "NO GLOBAL LOOP REALIGN"}}
};
static const ui_help_card_t HELP_FX_MORE[] = {
    {{"SELECT: CHOOSE VOICE", "CHANGES: NEXT ORIGINAL", "EXISTING ECHOES KEEP SETUP", "MIDI ONLY; NO AUDIO FX"}}
};
static const ui_help_card_t HELP_GLO_MORE[] = {
    {{"GLO: SETUP / LOOK / PERF", "K1 THEME ON APPEARANCE", "LIVE LOOK; SAVE WHEN IDLE", "K2 RETRIES A SAVE ERROR"}},
    {{"SYNC MODE: WHEN STOPPED", "EXT INPUT: USB/TRS/BLE", "ANY INPUT COMBINATION", "K4 LIGHTS; ROOT: SONG"}},
    {{"GLO MIDI: CLOCK / NOTES", "K1 CLOCK OUT: USB/BLE/BOTH", "K2 MIDI OUT: USB/BLE/BOTH", "CHANGE ROUTES WHEN STOPPED"}},
    {{"PERFORMANCE: K1 SLOT 01-10", "K2 LOAD / STORE / RENAME", "SEL PREPARES / CONFIRMS", "LOAD / STORE: STOP FIRST"}},
    {{"NAME: K3 CURSOR; K4 CHAR", "_ MEANS ASCII SPACE", "BS + SEL: ONE DELETE LEFT", "K3 LEAVES BS; SEL SAVES"}},
    {{"K1 / LEAVE: CANCEL EDITS", "OVERWRITE: SEL AGAIN", "STORE USED: KEEPS NAME", "SAVE: PROJECT AND SLOTS"}}
};
static const ui_help_card_t HELP_EDIT_MORE[] = {
    {{"HOLD VOICE + EDIT", "REGENERATE THAT VOICE", "WHEN PLAYING: LOCAL LOOP", "LOCKED: REGEN SKIPPED"}}
};
static const ui_help_card_t HELP_SEL_MORE[] = {
    {{"HOLD VOICE + SEL", "LOCK / UNLOCK THAT VOICE", "LOCK FREEZES AUTO CHANGES", "MANUAL CONFIG STILL WORKS"}}
};
static const ui_help_card_t HELP_SAVE_MORE[] = {
    {{"SAVE + C5-F5: STORE SLOT", "C5-F5 ALONE: RECALL SLOT", "RECALL NEEDS PLAYBACK", "SLOT STORAGE NEEDS SAVE"}}
};
static const ui_help_card_t HELP_OCT_MORE[] = {
    {{"HOME + OCT-: UNDO", "UNDO NEEDS PLAYBACK", "UNDO RECALL: NEXT BAR", "OCT- + OCT+: UPDATE, 5 S"}}
};
static const ui_help_card_t HELP_DICE_MORE[] = {
    {{"DICE REBUILDS MATERIAL", "INCLUDES LOCKED MATERIAL", "HOME + OCT-: UNDO", "COMPOSER CLOCK RETAINED"}}
};
static const struct { const ui_help_card_t *card; uint8_t n; } HELP_MORE[NB] = {
#define HELP_SET(b, a) [b] = {a, sizeof a / sizeof a[0]}
    HELP_SET(B_HOME, HELP_HOME_MORE), HELP_SET(B_ENV, HELP_ENV_MORE),
    HELP_SET(B_ARP, HELP_ARP_MORE), HELP_SET(B_LFO, HELP_LFO_MORE),
    HELP_SET(B_SEQ, HELP_SEQ_MORE), HELP_SET(B_FX, HELP_FX_MORE),
    HELP_SET(B_GLO, HELP_GLO_MORE), HELP_SET(B_EDIT, HELP_EDIT_MORE),
    HELP_SET(B_SEL, HELP_SEL_MORE), HELP_SET(B_SAVE, HELP_SAVE_MORE),
    HELP_SET(B_OCTDN, HELP_OCT_MORE), HELP_SET(B_OCTUP, HELP_DICE_MORE)
#undef HELP_SET
};

static int help_count(int b) { return b >= 0 && b < NB && b != B_PLAY ? 1 + HELP_MORE[b].n : 0; }

static int help_lines(int b, const char *l[HELP_LINES])
{
    int count = help_count(b), n = 0;
    if (!count) return 0;
    int part = ui.help_part % count;
    if (part) {
        for (int i = 0; i < HELP_LINES; i++) l[i] = HELP_MORE[b].card[part - 1].line[i];
        return HELP_LINES;
    }
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME:
        if (ui.page[V_PERFORM] == 2) {
            L("COMPOSE: K1 ON / OFF"); L("K2 FORM: NEXT SECTION"); L("K3 PHRASE BARS; K4 EVOLVE"); L(proj.composer.enabled ? "ALGORITHM: SELECT STAGE" : "ALGORITHM: DISABLED");
        } else if (ui.page[V_PERFORM]) {
            L("PLAY: K1 DIRECTION"); L("K2 GROOVE (ARP / MOTIF)"); L("K3 MIDI DELAY ON / OFF"); L("K4 MIDI DELAY TIME");
        } else {
            L("QUICK: K1 TRACK"); L("K2 GENERATOR"); L(heartbeat_selected() ? "K3 PARTS; K4 DRUM GROOVE" : "K3 ROOT; K4 SCALE"); L("TAP HOME: NEXT SUBPAGE");
        }
        break;
    case B_ENV:
        if (ui.page[V_VOICE]) {
            L("ROUTING: K1 MIDI CHANNEL"); L(heartbeat_selected() ? "K2 UNUSED; K3 DENSITY" : "K2 REGISTER; K3 DENSITY"); L("K4 COMPOSER TENSION"); L("MASTER: GLOBAL DENSITY");
        } else {
            L("TIMING: K1 RATE"); L("K2 GATE; K3 LOOP STEPS"); L("K4 PITCH PHASE"); L("TAP ENV: NEXT SUBPAGE");
        }
        break;
    case B_ARP: L("HARMONY: K1 GENERATOR"); L(heartbeat_selected() ? "DRUMS: K2 / K3 UNUSED" : "K2 VOICE ROOT; K3 SCALE"); L("K4 DIRECTION (ARP/MOTIF)"); L("SELECT: CHOOSE VOICE"); break;
    case B_LFO: L("CC LANES / SCENE STATUS"); L("K1-K4 ARE READ-ONLY"); L("KEYS SELECT CC SCENES"); L("MIDI CONTROLLERS, NO AUDIO"); break;
    case B_SEQ: L("MOTION: K1 GROOVE"); L("K2 LOOP LENGTH, 1-16"); L("K3 PHASE; K4 DIRECTION"); L("SELECT: CHOOSE VOICE"); break;
    case B_FX: L("MIDI DELAY: K1 ON / OFF"); L("K2 TIME; K3 FEEDBACK"); L("K4 VELOCITY DECAY"); L("DELAY ECHOES IGNORE CURSOR"); break;
    case B_GLO:
        if (ui.page[V_SETUP] == 3) {
            L("PERFORMANCE: K1 SLOT"); L("K2 LOAD / STORE / RENAME"); L("K3 POSITION; K4 CHARACTER"); L("SEL CONFIRMS; K1 CANCELS");
        } else if (ui.page[V_SETUP] == 2) {
            L("APPEARANCE: K1 THEME"); L("K2 RETRY IF SAVE FAILED"); L("K3 / K4 UNUSED"); L("STOP TO PERSIST LIVE EDITS");
        } else if (ui.page[V_SETUP] == 1) {
            L("GLO MIDI: K1 CLOCK OUT"); L("K2 MIDI OUT; K3/K4 NONE"); L("USB / BLE / USB + BLE"); L("STOP FIRST; SAVES WHEN IDLE");
        } else {
            L("SETUP: K1 GLOBAL ROOT"); L("K2 SYNC; K3 EXT INPUT"); L("K4 PANEL LIGHTS"); L("TAP GLO: APPEARANCE");
        }
        break;
    case B_EDIT: L("RE-ROLL UNLOCKED VOICES"); L("WHEN PLAYING: LOCAL LOOP"); L("HOLD VOICE + EDIT: ONE"); L("HOME + OCT-: UNDO"); break;
    case B_REC: L("REC: B PART AT NEXT BAR"); L("HOLD VOICE + REC:"); L("ONE BLANK LOCAL CYCLE"); L("PLAYBACK REQUIRED"); break;
    case B_SEL:
        if (performance_page()) {
            L("SEL: PREPARE / CONFIRM"); L("OVERWRITE: SECOND CONFIRM"); L("BS: ONE DELETE PER SEL"); L("K3 LEAVES BS; SEL SAVES");
        } else {
            L("ON ENV: LOCK / UNLOCK"); L("SELECTED VOICE ONLY"); L("ELSE HOLD A VOICE + SEL"); L("SELECT ENCODER: VOICE");
        }
        break;
    case B_SAVE: L("SAVE PROJECT AND SLOTS"); L("IF PLAYING: WAIT FOR STOP"); L("SAVE + SLOT: CAPTURE NOW"); L("GLOBAL SETTINGS AUTOSAVE"); break;
    case B_OCTDN: L("TAP: RE-ROLL UNLOCKED"); L("HOME + OCT-: UNDO"); L("LOCAL LOOP WHEN PLAYING"); L("KEEP HOLDING FOR COMBOS"); break;
    case B_OCTUP: L("DICE: NEW PROJECT SEED"); L("PLAYING: RECALL NEXT BAR"); L("STOPPED: READY FOR PLAY"); L("OCT- + OCT+: UPDATE, 5 S"); break;
    default: break;
    }
#undef L
    return n;
}
