# Per-voice musical configuration — TGM-005A-CORE1

**NOT TESTED.** No tests, sanitizers, validation scripts, builds or hardware
verification were run. This adds configuration infrastructure only.

Each materialized voice owns `config` (the active `bl_vconfig_t`), `pending`
(its latest requested target), and `config_dirty` (differing field bits).
The frame contains generator configuration, effective key including scale
intervals, register bounds, and loop duration. Existing templates initially
inherit their unchanged generator/key/register and actual phase-adjusted length,
including independent 3840/3600-tick plucks and legacy multi-bar material.

Interfaces are intended for the main loop:

| Layer | Get active/requested settings | Stage settings |
| --- | --- | --- |
| Engine, `BL_VID_` | `bl_live_get_config` | `bl_live_set_config`, `bl_live_set_config_field` |
| Runtime, `BL_VID_` | `rt_get_voice_config` | `rt_set_voice_config`, `rt_set_voice_config_field` |
| App project, template voice index | `proj_get_voice_config` (requested target) | `proj_set_voice_config`, `proj_set_voice_config_field` |

A full-frame setter validates the whole target before replacing the pending
frame. Field setters merge into the latest pending target, not the old active
frame. Invalid input returns -1 without committing a partial edit. Setting the
target back to the active values clears pending changes. Full-frame setters
also allow atomic register-range edits. App setters do not push undo points
themselves; the caller can use the existing `undo_push` before a batch.
Runtime/app edits are refused while a project replacement is pending.

Field selectors are `BL_CFG_GENERATOR`, `ROOT`, `SCALE`, `REG_LOW`, `REG_HIGH`,
`RATE`, `GATE`, `LOOP`, `DIRECTION`, `GROOVE`, and `PHASE` (all with the `BL_CFG_` prefix).
Generator IDs remain unchanged; roots are 0..11 and scale IDs 0..22. Register
limits are MIDI 0..127 with low <= high. RATE is 240 or 480 ticks and GATE is
positive and shorter than RATE. RATE/GATE/DIRECTION/GROOVE field setters require
an ARP or MOTIF target. If a shorter RATE invalidates the current gate, it selects
half that spacing; a full-frame setter can supply an explicit compatible gate.
ARP↔MOTIF switching preserves their timing settings. Switching between legacy
generators and ARP/MOTIF initializes the newly selected generator's a/b meanings
to one bar/four seed-procedure passes or eighth RATE/half-spacing gate.

LOOP accepts 1..16 sixteenth-note steps and stores `steps * 240` ticks. Existing
unedited multi-bar defaults remain valid; the full-frame interface retains tick
duration for those generators. CORE2 adds independent pitch PHASE, described
below. Existing `phase_steps` is used only when deriving template defaults.
Direction and groove use the existing enums and algorithms.

## Boundary behavior

Rendering finishes the current cycle using its original length. At that local
completion tick, the whole pending frame becomes active together. The next cycle
uses the new duration; shortening never truncates the current one. Separate
voices retain their own boundaries, without global-bar alignment or resetting
unrelated voices. Edits take effect after the already-rendered lookahead; no MIDI
events are removed or rewritten. With no pending changes, normal loop behavior
is unchanged.

LOCK permits manual configuration edits at the next local boundary and remains
set across material rebuilds. Automatic MOTIF evolution remains frozen. Inactive
voices retain pending edits until they resume and complete a local cycle. Blank
cycle boundaries can accept settings; they still do not count toward MOTIF
evolution. Original queued note-offs and echo tails remain transport-owned.

Generator, SCALE, RATE and LOOP edits rebuild only that voice's source material
using its existing pattern seed and the existing factories. MOTIF starts a new
stable foundation and four-cycle evolution delay; the old cycle is not counted
toward the new delay. ROOT, REGISTER, GATE, DIRECTION and GROOVE edits retain
current melodic substitutions and evolution RNG/countdown state. A direction
change explicitly starts the selected order; other non-rebuilding edits retain
its cursor. Manual regeneration uses the active frame, retains LOCK behavior,
and resets MOTIF's lifecycle as before. If regeneration and edits coincide, the
new frame is applied first. Already-requested blank/accent operations keep their
existing priority.

Each voice renders, regenerates, mutates and evolves in its own active key.
The existing global ROOT/SCALE control broadcasts a key into all pending frames;
each voice enters that key at its own local boundary, including while locked. The project key
remains the default/legacy global display value; per-voice getters expose the
actual effective and pending values.

## State and persistence

Snapshots/undo store both frames, the pending mask, current source material and
MOTIF evolution state. Local tick phase and active/blank flags are now saved for
all generators, so recall resumes the current cycle before applying pending
edits. The field mask must match the frame differences; invalid configuration
or inconsistent generator/length/register metadata is rejected during packing
and unpacking. The existing recall queue and undo storage are reused.
Source-loop phase is saved separately from the local completion-clock phase,
preserving legacy cases where activation had left those two clocks offset.

The app project stores optional explicit per-voice targets; absent overrides
retain template defaults. On stopping/saving, recalled or undone targets are
carried back into the project when its template still matches. Project files
store the requested next-Play configuration, while runtime snapshots preserve
the active/pending distinction and remaining local position.

Project format is **5** and snapshot slot format is **7**, with no migration.
Packed active/pending frames cost 29 bytes each plus a two-byte pending mask per
voice; legacy-generator records also gain five bytes of phase/flags. RAM uses
two fixed configuration frames per voice, with no duplicated 64-note patterns,
heap allocations, extra schedulers or unbounded queues. Existing slot/undo
capacity checks still apply; device padding and resource costs are unverified.

Legacy generators retain their existing truncate/extend behavior on explicit
length changes; extending their duration does not invent additional notes.
The 64-note capacity and existing gate-overlap/shortened-boundary limitations
remain. There are no UI bindings, delay-setting changes, channel
changes or MIDI scheduling changes in this task.

## Independent pitch PHASE — TGM-005A-CORE2

**NOT TESTED.** CORE2 was implemented without tests, sanitizers, validation
scripts, builds or hardware verification.

`bl_vconfig_t.phase` is a zero-based offset in ordered pitch-traversal positions,
not ticks. Set it with `proj_set_voice_config_field(voice, BL_CFG_PHASE, value)`
or the existing runtime/engine field setters. Existing getters return it in
active/requested configuration frames. `bl_config_phase_count(&target)` gives
the number of selectable target positions: the valid range is **0..count-1**.
ARP uses up to eight source tones (its two-octave cycle); MOTIF uses all stored
phrase positions, up to 64. Short patterns expose only their available prefix.
Legacy generators expose only PHASE 0. `bl_pitch_phase_count(pattern)` reports
the current actual source size, with an empty source exposing only position 0.

The offset skips positions along the selected direction's order, rather than
rotating MIDI numbers or editing source pitches. From a fresh UP traversal of
four pitches, PHASE 0 is `1 2 3 4 1...`, PHASE 1 is `2 3 4 1 2...`, and PHASE 2
is `3 4 1 2 3...`. DOWN/M-DOWN skip along their descending orders; pendulum
variants skip along their established ascending/descending traversal, retaining
endpoint handling and M-PENDULUM's one-time root pickup when starting at zero.
The range is based on source positions, not the longer pendulum cycle period.

An edit applies with the rest of the pending frame at the next local boundary,
including while LOCK is set. PHASE alone changes neither traversal cursor nor
MOTIF notes, protected source anchors, RNG states or evolution countdown. As
with directions, reordered audible notes need not place the root at each local
loop's first onset. Groove-suppressed positions still advance normally.

The offset is added to the current traversal cursor. Cursors continue across
15/16-step boundaries; PHASE is not reapplied as a loop restart. PHASE 0 takes
the exact existing path, including UP's historical stored-pattern repetition
at local boundaries. Nonzero UP uses the continuous cursor. Returning UP to
PHASE 0 restores that historical behavior; no cursor reset is added.

Timing, RATE, GATE, LOOP duration, legacy `phase_steps` and source-loop timing
offsets are unchanged. Field edits that change source size wrap PHASE modulo
the new target count; explicit out-of-range PHASE edits are rejected. Full-frame
setters require an already valid PHASE. Regeneration restarts traversal using
the retained offset and the existing lifecycle rules. If an existing arranger
operation thins the source, rendering folds the offset against the actual
remaining source size, avoiding invalid indices while retaining the requested
setting for later regeneration. For nonzero PHASE, source-size changes also
fold the traversal cursor to the new direction period without selecting another
direction. The zero-PHASE recovery path is left unchanged. Empty sources emit nothing.

Snapshots/undo store both active/pending PHASE values and the existing traversal
cursor, independently of local completion-clock and source-loop timing phase.
Pattern metadata reconstructs its PHASE from the active frame. CORE2 adds two
packed bytes per voice, one in each frame. The project format advances 4→5 and
slot format 6→7 even if native struct padding absorbs a byte; no migration is
provided. There is no extra runtime queue, duplicated pattern or MIDI change.
