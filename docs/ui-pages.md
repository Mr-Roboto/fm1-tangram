# FM-1 pages and K1–K4 — TGM-005A

**NOT TESTED.** No tests, scripts, sanitizers, builds, flashing or hardware
verification were performed.

| Section/page | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| HOME QUICK | Track | Generator | Root | Scale |
| HOME PLAY | Direction | Groove | Delay ON/OFF | Delay time |
| ENV TIMING | Rate | Gate | Loop length | Pitch phase |
| ENV ROUTING | MIDI channel | Register | Unused | Unused |
| ARP HARMONY | Generator | Root | Scale | Direction |
| SEQ MOTION | Groove | Loop length | Pitch phase | Direction |
| FX MIDI DELAY | Delay ON/OFF | Time | Feedback | Decay |

Short-pressing the active section cycles its pages; HOME and ENV each have two.
Switching away and returning restores that section's last page. Page memory is
runtime-only and starts at QUICK/TIMING after initialization. Other sections
have one page. Holding a section for about 1000 ms retains the existing help
card; releasing a long press does not change pages. HOME + SELECT still changes
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
SEQ now shows the source grid; FX shows actual MIDI delay state/time per voice.
Compact knob labels use K1–K4, with full generator/scale names in screen-reader
announcements. Preview refresh includes the new project settings. Display and
screen-reader behavior have not been verified.

LFO and GLO retain their existing pages and bindings. GLO ROOT and ALGORITHM
retain their existing global key-control behavior; HOME/ARP ROOT and SCALE edit
the selected voice. Transport, voice-key combinations, SEL lock on ENV, SAVE and
snapshot keys, OCT+ Dice, update-mode combination, MASTER density,
and PRESETS are retained. Parameter edits use the existing undo-point mechanism;
existing stopped-transport undo semantics are unchanged.
OCT− short alone now invokes MUTATE using the existing unlocked-voice re-roll
mechanism (also retained on EDIT); HOME + OCT− invokes the existing undo path.

AUTO, FORM, AMBIENT and RATCHET retain their implementations and stored settings;
their old knob access is deferred. No replacement shortcuts or extra pages were
added. There are no engine, MIDI scheduler, persistence-format, startup branding
or general palette/layout changes in this task.

## Readability pass — TGM-005C

**NOT TESTED.** No tests, scripts, sanitizers, builds or device previews were run.
The replaced Tangram pages retain their assignments and use bounded text areas
within the existing 240×240 display layout. K1–K4 identifiers and compact labels
are separated from indicators and values; font selection checks both width and
height, with dots for text that cannot fit. Section titles stay inside the
display band, and HOME/ENV show their existing page position as 1/2 or 2/2.
Voice titles reserve room for status badges, while routing/voice rows and MIDI
delay table columns have explicit text bounds. Musical values are unchanged.
LFO/GLO keep their original rendering path; all input/SHIFT/help handling remains
unchanged. Long text may still require a small font or truncation. Physical
legibility, font metrics and final appearance remain unverified.
