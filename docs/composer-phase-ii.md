# Composer Phase II

**NOT TESTED.** Implementation was reviewed by static source inspection only.
No build, automated tests, installation or flashing was performed. Internal /
external clock behavior, memory fit and hardware display behavior are unverified.

Phase II extends the same Composer bar callback, theme buffer and voice-local
completion path. No second arranger, clock, full-pattern bank or event queue
was added. All six themes, global settings, firmware identity and physical
controls retain their existing definitions.

## Section transitions

Activity ramps remember the previous section's computed role activity, rather
than assuming the preceding chronological section was played to completion.
Manual stage jumps therefore retain a coherent departure point. Bass/first
pluck/drone start first; second pluck/pad wait one bar; lead/rhythm wait two.
Normal ramps take four bars after this offset; B takes `4 + 2 × Tension` bars.
Short sections can leave a ramp incomplete; the next transition carries it on.
Fresh non-Intro starts instead initialize that section's own role activity:
there is no transition from an unplayed Intro. Section age remains zero and
the full selected dwell is retained, preserving Phase II checkpoint timing.

B's shared harmonic lift starts at one scale degree, then increases every two
bars toward WARM +3, LIFT +4 or PEAK +5 during live transitions. Starting directly
in B initializes the selected target at the first engine boundary, respecting
LOCK/manual harmony. All participating voices use the same
schedule, over the captured theme's underlying harmonic progression. Breakdown
removes one degree every two bars until home. HOME adds no B lift. A full
schedule refuses a new event. Manual harmony edits and any LOCK block these
automatic moves. Reducing a macro does not jump an already established B lift
downward; Breakdown releases it. Short sections may not reach the maximum.

## Loop and rate development

The first A captures the existing theme buffer. One active, eligible melodic
voice is selected from that material, preferring Pluck 2, then Lead, then Pluck
1. Selection is seed/cycle deterministic and remains fixed until a later cycle;
manual takeover does not immediately move automation onto another voice.
Timing eligibility requires an ARP/MOTIF foundation with 15 or 16 sixteenth
steps, a 240/480-tick rate and onsets aligned to that rate. Legacy generators,
multi-bar, shorter and irregular grids retain their established timing.

| Evolution | Development | B |
| --- | --- | --- |
| OFF / GENTLE | Original loop/rate | Original loop/rate |
| MOVING | After four elapsed section bars: 15 steps | 15 steps, original rate |
| ACTIVE | After four bars: 15; after eight: 13 steps | 13 steps; after two bars, LIFT/PEAK permits the opposite 240/480 rate |

The selected source is edited only at its next local completion after a bar
decision. Already-rendered cycles finish first. Each new request replaces that
voice's previous target instead of stacking edits. Breakdown/Intro/A/Outro and
Evolution OFF request original timing again. Return uses the existing full
phase-preserving theme recall rather than retiming its material.

Retiming repositions existing source ordinals on the chosen 1/16 or 1/8 grid.
Retained slots keep their current pitches, velocities, accents and ratchets;
missing suffix slots restore captured A notes. It does not call a generator or
draw RNG. Gate uses the captured duty ratio at the new supported spacing.
Faster playback of an eighth-note foundation creates a compact phrase followed
by space: no extra notes are invented to fill the faster grid. Slower/shorter
variants omit suffixes until restoration. Natural MOTIF evolution retains its
RNG state and countdown and continues through normal completed cycles.

## Register development

Eligible upper melodic voices use their captured A register as an outer bound.
Ranges narrower than two octaves are left alone. GENTLE/MOVING/ACTIVE permit up
to one/two/three twelve-semitone boundary moves, while retaining at least a
twelve-semitone window inside that original range. Development raises the low
bound once every four bars. B carries established height and may raise further;
it does not first lower Development's register. Breakdown lowers the high bound
starting after two bars, then every four bars. Other sections restore the full
captured bounds. Existing octave folding supplies notes inside this window;
scale degrees and the shared harmonic schedule are not replaced.

Bass and sustained support keep their register and timing as anchors. Register
edits and timing edits share the same local-boundary transaction and preserve
delay/channel settings. No gate is cut and no queued off/echo is cancelled.

## Ownership, phase and display

Automatic targets are separate from manual `pending/config_dirty` frames.
Staging them neither claims manual ownership nor temporarily bypasses Composer
density. LOCK, regeneration, blanking, activation and configuration edits cancel
that voice's pending automatic target. Manual ownership persists until next Play
as before. Captured A remains immutable. Disabling Composer cancels pending
automation and hands current frames to the existing controller without an
implicit reset. Pausing AUTO holds section time; targets already requested may
still reach their independent voice boundaries.

At a timing edit the old source phase is expressed in onset units and folded
into the new duration; other voices' phases are untouched. Direction cursors
fold to the new source period without restarting direction or M-PEND pickup.
Nonzero pitch PHASE folds if the new source is shorter. Return restores the
original captured source phase, completion phase, direction cursor and frames
through the existing reprise contract, excluding protected voices.

HOME COMPOSE shows `AUTO FRAMES / NEXT LOOP` while frames are developed or
pending. Existing voice controls/grid show applied loop, rate and register.
Original Note-Ons carry the source grid identity through the existing metadata
pipeline. If render-ahead has replaced that grid, the old source marker hides
until a matching original dispatch arrives. Note-Offs/echoes do not select it.
Grey timing markers retain the same fixed 16-step scaling and density-zero time.

## Persistence and limits

No project or slot version changes are needed: applied settings and actual
source notes already fit project format 7 / slot format 9. Readers for older
projects/slots remain unchanged. On Stop/SAVE, existing configuration promotion
can make developed settings the next-Play project target. Projects store
settings/seed, not a complete evolved pattern; slots retain actual source/state.
Neither format serializes the Composer clock, theme buffer, activity ramps or
pending automatic targets. Recall/undo explicitly takes ownership and cancels
those requests rather than resuming an incomplete autonomous transaction.

State remains fixed-size: seven compact motion targets and role ramp values;
the existing capped-pattern scratch is reused. Grid metadata adds four raw
bytes per queued event and rendered note, potentially more with ABI padding
(1,280 event slots and 128 rendered-note slots). Firmware/RAM fit and CPU cost
require an authorized build and hardware review. Grid identity uses a bounded
32-bit hash of duration/onset positions; it is display metadata only.

Deferred: timing development of arbitrary legacy/multi-bar sources, restoration
of discarded suffix evolution beyond the captured theme, resumable Composer
checkpoints, and independent register/transition macro menus. No unsupported
rate modes, LCM scheduling or random full-pattern replacement were introduced.

## Selected-start regression coverage

`tests/host/composer_start_test.c` covers every non-Intro starting section and
Tension level, full role/renderer activity, repeated next-Play initialization,
initial B harmony, zero section age/full dwell, unchanged Intro buildup, live
navigation ramps, AUTO pause/resume, controller enable, LOCK and manual harmony.
It also checks that startup does not invent a theme or regenerate source material.
The focused runner is `sh tests/host/run_composer_start.sh`; it chains no other
suites. These tests were added but **not compiled or executed** for this fix.
