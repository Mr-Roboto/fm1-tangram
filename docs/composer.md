# Tangram Composer

Status: static source inspection only. No build, tests, timing/memory measurements
or hardware verification have been performed for this milestone.

Composer is opt-in and owns the existing AUTO/arranger bar callback. It replaces
legacy arranger decisions while enabled, without another sequencer or clock.
OFF resumes the configured legacy arranger behavior, including its eventual
stop. Controller changes do not silence held notes or restart voice loops.
An AUTO-off action pauses Composer's section clock while retaining its current
activity targets; AUTO-on resumes that clock without restarting the composition.

## Hardware controls

HOME cycles QUICK → PLAY → COMPOSE. QUICK/PLAY mappings remain unchanged.

| COMPOSE encoder | Function |
| --- | --- |
| K1 | Composer OFF/ON at the next engine bar boundary |
| K2 | CLASSIC, SLOW BUILD or AMBIENT form; choose while stopped |
| K3 | Phrase unit: AUTO or 4–64 bars, in increments of four |
| K4 | Composer Evolution: OFF, GENTLE, MOVING, ACTIVE |

ENV ROUTING retains MIDI channel/register on K1/K2. Previously unused K3 sets
selected-voice density, 0–100%; K4 is project-wide Composer Tension: HOME, WARM,
LIFT, PEAK. MASTER remains global density/Dynamics. No physical pages, rate
modes, synthesis, audio effects or routing were added. ALGORITHM remains
inactive on HOME QUICK/PLAY. On HOME COMPOSE it selects the Composer stage:
Intro, A, Development, B, Breakdown, Return, Outro. Each detent moves one stage,
without acceleration or wrapping. With COMPOSE OFF, ALGORITHM does not change
the section selection and the accent underline is hidden. Re-enabling restores
navigation with the stored selection retained. Other pages retain their
ALGORITHM mapping, except Heartbeat's ARP page disables global scale editing.

While playing, enable Composer first. Turning ALGORITHM replaces the single
pending stage target for the next safe engine bar, after already-rendered music.
CURRENT uses a filled accent tile; the ALGORITHM target uses an accent underline,
with pending text in the secondary accent. Turning back to
the current stage cancels the pending target without restarting that section.
Selection takes precedence over queued Advance actions on that bar; those
Advance actions remain queued for the next running bar. AUTO-off keeps the
clock paused but still permits an explicit stage selection at a bar boundary.

While stopped, ALGORITHM selects START for the next Play (Composer must be
enabled to change or use it). This session-local choice resets to Intro on project load
or template change and is not saved in projects or snapshots. OCT- retains its
re-roll action, HOME + OCT- retains undo, and OCT+ retains Dice; stage selection
does not add an undo point. Starting/jumping to
Return before A has been captured retains the existing material: no theme is
invented. Stage selection consumes no RNG, does not increment the cycle, and
uses existing section-entry/LOCK/reprise logic without blanket regeneration.

A fresh non-Intro start uses the selected section's role activity immediately.
Starting B applies its selected Tension target at the first engine bar boundary,
subject to LOCK/manual harmony protection. This does not advance section age,
shorten its countdown or fast-forward Phase II evolution. Intro retains its
normal sparse buildup. Live section changes retain their transition ramps.
The STOP/PLAY starting choice and AUTO pause/resume paths remain unchanged;
resuming AUTO does not reinitialize the current section.

COMPOSE shows section, cycle, bars remaining and manually owned/locked voices.
The existing horizon uses Composer section names; pending controls appear as
COMPOSER. Settings apply without restarting the current section. A phrase-unit
edit changes the next section, not the current countdown.

To prepare an autonomous performance, choose the form and voice configurations
while stopped, enable Composer, then press Play. Manual ownership is clear at
each new Play. During performance, editing a voice deliberately gives it back
to the player rather than letting automation replace the edit later.

## Musical form

Each cycle follows Introduction, Theme A, Development, B, Breakdown, Return,
Outro, then another Introduction rather than Stop. Lengths in phrase units are
1, 2, 2, 1, 1, 2, 1. AUTO derives a four-bar-aligned unit from the selected
form's period range using a separate seed-derived RNG; AMBIENT uses at least
32 bars. The automatic unit is chosen once per cycle. Explicit units stay fixed.
Queued Advance actions move to the next section at their existing bar boundary.

Role activity uses staggered ramps from the actual previous section's activity,
including when manual navigation interrupts a ramp. Bass/first pluck lead upper
voices; B uses longer ramps as Tension increases. Introduction and
breakdown emphasize sparse plucks and sustained support; A and Return restore
upper activity; B favors the second pluck and lead. Onsets are thinned/enriched
without changing ticks, gates, groove, traversal, ratchets or loop lengths.
Initially active voices keep processing their own cycles at zero activity. A
live handover retains currently inactive voices rather than assuming the user
wants them activated.

The first Theme A captures the existing theme snapshot buffer. Return restores
only eligible voices with the same source-phase and separate completion-phase
logic as phase-preserving reprise. Channels and delay settings are retained.
The initial A remains the reference across cycles. There is no extra full-pattern
snapshot, blanket regeneration or forced global-bar alignment.

## Evolution, tension and manual control

GENTLE requests one local-boundary accent change at Development/B entry. MOVING
adds checkpoints every eight bars there. ACTIVE may request an existing bounded
mutation of an eligible legacy melodic generator. Only one supporting pluck/lead
is requested per checkpoint; bass and sustained support keep their material.
ARP/MOTIF pitch generation and MOTIF evolution/RNG streams remain intact.
Evolution OFF restores eligible developed timing/register frames at local
boundaries, without disabling the generator's own evolution. Phase II adds
source-preserving loop/rate and bounded register development; see
[Composer Phase II](composer-phase-ii.md) for exact eligibility and timing.

Tension sets B's melodic activity and optionally adds a shared scale-degree
lift during B: HOME none, WARM up to +3, LIFT +4, PEAK +5, one degree at entry
then another every two bars. Breakdown releases the lift on that same cadence.
Short live transitions may end before reaching the target; a fresh B start uses
the selected target immediately. Bass and upper voices use
the same schedule. Return restores the theme schedule only when no locked voice
or explicit harmonic intervention would be overridden. A density/register edit
does not freeze the ensemble's shared harmonic progression; manual per-voice
ROOT/SCALE changes do protect it. Shared scene automation also defers to LOCK
and manual scene selection.
A full schedule skips the lift. Macro edits update activity on the next bar;
the active B lift never jumps downward on a macro edit; Breakdown provides release.

Manual configuration, regeneration, blanking, activation and LOCK take a voice
out of Composer shaping/development/reprise until Stop/Play. Unlock alone does
not give it back to automation. Pending Composer accent/nudge and timing/register requests are
cancelled on takeover. Key changes and snapshot recall claim affected voices;
transposition/B-part protects harmony; manual scenes protect scene control.
Other voices and the section clock continue. MASTER is never automated in
Composer mode. Pending manual changes bind before Composer's bar decision.

Recall/undo restores musical frames, density and phases without rewinding the
Composer clock. Existing note-offs, channels, delay routing and echo tails
retain their ownership and timing.

## Density and persistence

Effective source density is `M × (100 - density_cut) / 100 × A / 100`, where
`M` is active MASTER density in 0..1 and `A` is Composer's integer role activity
percentage (including its existing four-bar section ramp and B Tension target).
The multiplication order is MASTER, voice factor, then activity factor, using
the engine's software float arithmetic. MASTER zero returns zero immediately.
`A = 100` unless Composer is enabled and owns the voice: not manually owned,
not locked, no pending configuration, regeneration or blank request. AUTO pause
retains its activity target. This navigation change does not alter the formula.

For example, MASTER 80%, voice 50%, and role activity 70% gives
`0.8 × 0.5 × 0.7 = 0.28` (28%). The existing density filter keeps
`ceil(source note count × effective density)` ranked notes, then Groove can
suppress further onsets. For 16 source notes this example retains five before
Groove; it does not mean every cycle produces exactly 28% of MIDI Note-Ons.
Voice density joins the pending configuration frame at each voice's next local
completion, including while locked. Older/zero-initialized frames mean 100%.
Direction maps pitches before density; Groove may suppress additional slots.
Display shading uses the renderer's same effective-density helper.

MASTER zero suppresses newly rendered pattern notes and TENSION one-shots while
transport, phases, Composer and evolution continue. Already queued MIDI messages,
note-offs and captured delay tails are not retroactively cancelled.

Project format 7 stores settings and density, reading formats 5/6 with Composer
OFF/full voice density. Snapshot format 9 stores both density frames and reads
format-8 slots. Loading never rewrites flash. Existing packed-state capacity
checks still refuse oversized snapshots rather than truncate them.

The same saved seed/settings and session starting-stage choice restart the same
composition; loading the saved project defaults that stage to Introduction.
Live cycle/RNG position is not a resumable song file. Template changes keep
their existing reset semantics and take their own settings (templates default
Composer OFF).

## Deferred

Timing evolution for legacy, multi-bar and short/irregular foundations,
automatic pitch PHASE changes, a separate register macro, arbitrary ARP/MOTIF
pitch mutation, resumable Composer checkpoints and live form replacement are deferred.
Existing TIMING/MOTION controls, including 15/16 phasing, retain local-boundary
semantics and take manual ownership. No new rates, LCM calculations or event queues
were added. Original Note-Ons carry an extra display grid identity through the
existing scheduling buffers. Firmware fit and runtime timing remain
unverified until an explicitly authorized build and hardware review.
