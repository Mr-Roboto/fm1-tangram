# FM-1 pages and K1–K4

**BLE routing extension (11 November 2026):** the host/device builds,
focused device-settings regression and combined simulator smoke gate passed.
On the FM-1/iPad, the user confirmed all six practical BLE hardware checks:
connection, MIDI note output into Korg iM1, persistence across reboot,
MIDI Clock output and input over BLE, simultaneous USB+BLE routing and
reconnection. Long-duration stability and quantitative clock-jitter/latency
measurements were not part of these checks.

| Section/page | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| HOME QUICK | Track | Generator | Root | Scale |
| HOME PLAY | Direction | Groove | Delay ON/OFF | Delay time |
| HOME COMPOSE | Composer ON/OFF (NEXT BAR) | Form (stopped: immediate; PLAY: next section) | Phrase bars (config NEXT BAR; section length on next section) | Evolution (NEXT BAR) |
| ENV TIMING | Rate | Gate | Loop length | Pitch phase |
| ENV VOICE | MIDI channel | Register | Voice density | Composer tension |
| ARP HARMONY | Generator | Root | Scale | Direction |
| LFO EDIT | CC Target (OFF/1/11/10/71/74/75) | Cycle (0.5–64 bars) | Depth (0–100%) | Center (0–100%) |
| SEQ MOTION | Groove | Loop length | Pitch phase | Direction |
| FX MIDI DELAY | Delay ON/OFF | Time | Feedback | Decay |
| FX MIXER | Volume CC7 (100 default) | Bipolar Pan CC10 (center 0, -64..+64) | Cutoff CC74 (70 default) | Resonance CC71 (30 default) |
| GLO SETUP | Musical root | Sync | External clock input (USB/TRS/BLE and combinations) | Lights |
| GLO MIDI | Clock output (USB/BLE/both) | Notes/CC output (USB/BLE/both) | Unused | Unused |
| GLO APPEARANCE | Global theme | Retry failed global save | Unused | Unused |
| GLO PERFORMANCE | Preset slot | Load/Save/Rename | Position | Character |

HOME COMPOSE also shows read-only TIME (`MM:SS`), PART (played bars in the heard
section) and TOTAL (played bars this performance). They replace its CONTROL/MASTER
status rows without changing encoder controls. STOP freezes all counters and
fractions without changing the selected starting section; stopped selection
changes preserve the final statistics. Only a newly accepted PLAY starts from
zero and clears old fractions/cues; repeated PLAY while playing does not. During uninterrupted
playback, section changes reset PART only. Silence/density zero still counts. Preroll
and stopped time do not. See [Composer counter semantics and reset conditions](composer.md#playback-counters).

Heartbeat exceptions: HOME QUICK keeps K3 PARTS / K4 drum GROOVE; ARP HARMONY
disables K2 Root / K3 Scale and ALGORITHM; ENV VOICE disables K2 Register.
The hidden controls do not edit voice or global pitch settings. Melodic
voices retain the mappings above. See [Heartbeat drums](heartbeat.md).

PRESETS clamps in the existing Demo / Full / Ambient order. Turning beyond an
endpoint leaves the selected preset and seed intact.

Short-pressing the active section cycles its pages; HOME has three, ENV two,
GLO four (SETUP → MIDI → APPEARANCE → PERFORMANCE). The GLO help overlay
uses seven *help cards*, not seven GLO subpages; its heading is explicitly
labelled `GLO HELP x/7` in the Tangram 1.0 UX pilot.
Switching away and returning restores that section's last page. Page memory is
runtime-only and starts at QUICK/TIMING after initialization. Other sections
have one page. Holding a section for about 1000 ms opens context-sensitive help;
keep holding for the next card every five seconds. Releasing a long press does
not change pages. See [themes and help](ui-themes-and-help.md). HOME + SELECT still changes
tempo and consumes HOME's release. TGM-005D adds momentary HOME + OCT− undo:
the chord acts once when both buttons become held, consumes both releases and
suppresses HOME help/navigation and OCT− mutation. Re-pressing a chord button
while the other remains held constitutes a new chord press. No SHIFT state is
latched after release. OCT−/OCT+ update mode retains priority when both octave
buttons are held at chord recognition. This chord handling is **NOT TESTED**.

K1 TRACK and SELECT use the same existing voice selection and skip roles absent
from the template. Duplicate page controls use the same CORE configuration,
not copies. Musical changes merge into the selected voice's latest pending frame
and apply at its next local boundary, including while LOCK is on. Register moves
the existing window by octaves, preserving width and clamping at MIDI limits;
its display uses note names. Gate displays a percentage of the currently selected
RATE, with fine duration steps; its underlying duration remains independent.
RATE choices are 1/8 and 1/16. LOOP edits select 1..16 steps. Unedited legacy
multi-bar template lengths remain visible in steps; their first length edit
enters the supported 1..16 range. Pitch PHASE uses CORE2's exposed target range.

RATE/GATE/DIRECTION/GROOVE show N/A for legacy generators where CORE1 does not
define them; select ARP or MOTIF to edit them. Legacy generators are retained
in GENERATOR selection. A single-position PHASE is fixed at 0. Pending project
replacement temporarily makes voice edits unavailable.

Delay/channel controls use CORE3's next-original-note behavior. Delay time is
ordered `1/16, 1/8, 1/8D, 1/4, 5/16, 1/4D, 7/16`. Feedback is strictly
25%, 50%, 75% or 100%; toggling delay does not reset it. Decay is 50..100%.
Existing echoes and active-note offs retain their captured settings/channels.

The existing screen layout, selected-source grid and voice overview are reused.
In the separate Tangram 1.0 UX pilot, knob-strip rings/pips show the actual
active parameter in the normal theme focus color, while a brighter second
marker indicates a differently staged target. The textual value remains the
requested value; the existing transient NEXT BAR/LOOP/SECTION messages remain.
This is supported only for controls where actual versus staged values can be
read reliably from existing state; MIDI-Delay echo generations and other
per-event state are intentionally not approximated. Composer's implicit
Classic Arc form is displayed as `CLASSIC` instead of `NONE` while enabled;
`CYCLE` and `NEXT` have separate information columns without changing font
sizes or the musical HOME screen.
SEQ now shows the source grid; FX shows actual MIDI delay state/time per voice.
Compact knob labels use K1–K4, with full generator/scale names in screen-reader
announcements. Preview refresh includes the new project settings. Display and
screen-reader behavior have not been verified.

LFO is now editable per selected voice, including OFF/previously unmapped lanes. Scene morphs remain controlled by the existing black-key scene actions. FX now has MIDI DELAY and MIXER subpages, with distinct CC4 controls per selected channel. GLO SETUP keeps ROOT / SYNC / EXT INPUT / LIGHTS.
EXT INPUT offers USB, TRS, BLE, USB+TRS, USB+BLE, TRS+BLE and USB+TRS+BLE.
GLO MIDI follows immediately, with independent CLOCK OUT and MIDI OUT choices
(USB, BLE or both); output changes require stopped transport and drained MIDI.
GLO APPEARANCE keeps theme and failed-save retry; GLO PERFORMANCE remains the
last page. All routing is device-wide, independent of projects. Changes persist
once settled and safely idle; no UI handler writes flash. See
[device-settings.md](device-settings.md) for host aliases and reboot limitations.
GLO ROOT and ALGORITHM outside HOME
retain global key-control behavior, except ALGORITHM on Heartbeat's ARP page.
HOME QUICK/PLAY ignore ALGORITHM and HOME COMPOSE uses it for stage selection
only while COMPOSE is ON. Turning it while OFF preserves the stored selection;
the target underline is hidden until enabled again. HOME/ARP ROOT and SCALE edit
the selected voice. Transport, voice-key combinations, SEL lock on ENV, SAVE and
snapshot keys, OCT+ Dice, update-mode combination, MASTER density,
and PRESETS are retained. Parameter edits use the existing undo-point mechanism;
existing stopped-transport undo semantics are unchanged.
OCT− short alone now invokes MUTATE using the existing unlocked-voice re-roll
mechanism (also retained on EDIT); HOME + OCT− invokes the existing undo path.

AUTO, AMBIENT and RATCHET retain their implementations and stored settings;
their old knob access is deferred. FORM is available on HOME COMPOSE K2.
While stopped, a change applies immediately. While playing with Composer
active, a new form is staged for the next section entry (automatic, manual
Advance, or manual section selection); the current section is not shortened.
The latest selection replaces the preceding pending form, and returning to
the already-active form cancels the pending change. With Composer disabled,
live FORM edits are refused. The engine does not restart and existing note
ownership, phase, delay tails and snapshots are retained.
Verified on FM-1 hardware (11 November 2026): live FORM switching and section
navigation work while playing, with no reported playback interruption. When
PLAY is stopped, the original START section selected while stopped is still
the next PLAY's starting point; live section choices do not overwrite it.
Composer details are in [composer.md](composer.md). Global theme changes do not
change musical project/slot formats, engine behavior or MIDI scheduling.

HOME QUICK/PLAY use one shared row of three 2x5 neutral grey marks below all
pattern strips, at steps 5, 9 and 13. Their main-panel coordinates are
(114,83), (140,83), (166,83), or display y=111 after the 28-pixel header.
The row is clear of strips/playheads and the HORIZON area. Individual strips,
including ENV's single strip, have no quarter-note ticks or dividers. Fixed
16-step spacing and both playheads are retained.

## Readability pass — TGM-005C

**NOT TESTED.** No tests, scripts, sanitizers, builds or device previews were run.
The replaced Tangram pages retain their assignments and use bounded text areas
within the existing 240×240 display layout. K1–K4 identifiers and compact labels
are separated from indicators and values; font selection checks both width and
height, with dots for text that cannot fit. Section titles stay inside the
display band, and HOME/ENV show their existing page position as 1/2 or 2/2.
Voice titles reserve room for status badges, while routing/voice rows and MIDI
delay table columns have explicit text bounds. Musical values are unchanged.
LFO and GLO retain the same screen bands; GLO and all modifier behavior remains
unchanged. Long text may still require a small font or truncation. Physical
legibility, font metrics and final appearance remain unverified.
