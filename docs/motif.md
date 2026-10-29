# MOTIF foundation and controlled evolution — TGM-002A / TGM-002B

`BL_GEN_MOTIF` is appended as ID 8 (existing generator IDs 0–7 stay unchanged).
It calls the ARP generator for its entire foundation: the same scale-aware chord
selection, ascending two-octave degree sequence, straight onset grid, gate and
duration handling. It then substitutes a few pitches once, at generation time.
It uses the existing live engine, harmony, register folding, MIDI and snapshots.
Demo Trio, Full Ensemble and Ambient Drift remain unchanged.

```c
voice.gen = (bl_gencfg_t){BL_GEN_MOTIF, 480, 73};
voice.phase_steps = -1; /* optional: 3600 ticks rather than the standard 3840 */
```

Configuration is the [ARP contract](arpeggiator.md): `a` is onset spacing
(480 eighths or 240 sixteenths; zero/invalid selects 480), `b` is independent
gate ticks (positive and shorter than spacing; zero/invalid selects half spacing).
RATE never sets duration. `phase_steps` adjusts duration in 240-tick units before
generation. Factory default duration is one bar; `bl_make_motif` exposes the same
explicit-duration and omitted-note-count contract as `bl_make_arp`.

Each replacement must be one neighboring scale degree above or below its original
chord tone, outside the four selected chord pitch classes. It must lie strictly
between the preceding and following original chord tones in an ascending segment.
Both resulting intervals must be at most five semitones (a perfect fourth).
The candidate stays inside the two-octave scale-degree range and introduces no
accidentals. Phrase ends and pitch-cycle wraps are left alone. A seeded SplitMix64
stream selects among suitable positions and candidates. After a replacement,
its adjacent positions are excluded so original chord tones still surround it.
If nothing suitable remains, generation stops without forcing a replacement.

Protected anchors are the first local onset (root) and the first onset at or after
the local duration's midpoint. In a standard one-bar pattern the latter is at
1920 ticks. This also gives 1920 for both supported RATEs in a 3600-tick loop.
Anchor decisions never use global ticks or bar parity. For an onset-truncated
long loop whose midpoint has no stored onset, the absent anchor is not inserted.

The maximum substitution budget is `floor(stored_onsets / 8)`: one for eight,
two for sixteen, zero for fewer than eight, and no more than 12.5% for other
counts. Actual substitutions can be fewer or zero. Event count, note timing,
gates, velocities, accents, ratchets and pattern duration stay as ARP generated
them. Identical key/configuration/seed/duration produces identical material;
different seeds can select different color tones. Existing rendered velocity
humanization still applies.

The factory still generates the stable TGM-002A motif. In the live engine,
TGM-002B adds the following independent lifecycle, bypassing the unrestricted
Legitimizer without setting LOCK.

## Local evolution

The first four complete local cycles play the original motif. The first
opportunity is at their end, so cycle 5 may use evolved material. After every
opportunity, including a no-change outcome, the next interval is sampled uniformly
from 2, 4, 6 or 8 complete cycles. Each voice owns separate SplitMix64 states
derived from its pattern seed and voice ID, with distinct `motif-interval` and
`motif-melody` labels. Timing choices do not depend on candidate availability,
other voices, render chunk sizes, global bar parity, UI polling or wall time.

For example, the host fixture's 16-step pluck has opportunities at local cycles
5, 13, 15, 17, 23, 29... (ticks 15360, 46080, 53760, 61440, 84480, 107520...).
The independent 15-step pluck has cycles 5, 13, 19, 21, 23, 31...
(ticks 14400, 43200, 64800, 72000, 79200, 108000...). Cycles are counted at the
voice's actual local loop boundaries; these are not quantized to global bars.

One opportunity operates on at most one variation slot: restore its original
chord tone, replace it with another valid neighbor, or relocate it to another
valid unprotected position. A vacant slot can be filled within the same budget.
Relocation restores one pitch and changes one other pitch as a single operation.
The four stored chord-degree indices plus the original scale count reconstruct
the original eight-tone ARP cycle, so evolution never uses the previous variant
as its harmonic reference. All candidate, anchor, adjacency and budget rules
above remain mandatory. An unavailable operation keeps the current motif.

Only pitch degrees change. The pattern seed also stays fixed, preserving existing
velocity humanization. Evolution touches neither note positions, gates, accents,
ratchets, length, channels nor the harmony schedule. Interior pitch edits occur
at safe local boundaries; already queued MIDI note-offs retain their original
pitches in the existing transport. There is no new scheduler or boundary correction.

LOCK freezes the remaining evolution cycles and both RNG streams. Unlock resumes
that countdown without catch-up. Blank cycles and inactive voices do not advance
it. Manual regeneration retains the existing local-boundary request behavior,
creates a fresh stable motif with the existing new seed/current key, reinitializes
both streams and restarts the four-cycle delay. LOCK still blocks regeneration.
Existing arranger/density actions retain their behavior. If an external action
alters the grid or makes the stored foundation/current substitutions unsuitable
in a new scale frame, opportunities continue but pitch evolution declines changes;
manual regeneration creates a compatible new foundation.

## Snapshots, undo and resources

MOTIF snapshots retain current notes, LOCK, the remaining countdown, both RNG
states, the compact original foundation, active/blank status and the captured local phase. On recall
(still bound through the existing bar-quantized action queue), MOTIF resumes
that local phase rather than restarting its loop. Subsequent onsets and evolution
match uninterrupted continuation of the captured motif. Existing queued note-offs
are handled by the transport. Undo uses the same capture/pack/recall path.
Non-MOTIF recall behavior is unchanged.

The snapshot slot wrapper is now format 2; no historical snapshot migration is
provided in this greenfield project. Raw state records add 27 bytes per MOTIF
voice (two u64 RNG states, six u8 countdown/foundation fields, one u32 phase, one u8 active/blank flag).
Other generator records are unchanged. Invalid metadata and slot overflows are
rejected. A pair of 16/15-onset MOTIF voices with an empty harmony schedule
needs 394 packed bytes.

Evolution state occupies 24 bytes per voice; snapshot structures also have four
phase bytes and one active/blank flag byte per voice. On the host ABI, `bl_state_t` grows from 16840 to 17048
bytes (+208), and `bl_live_t` from 76096 to 76912 (+816), including its embedded
recall/theme/capped copies. Device ABI padding may differ. Scratch arrays hold
at most 64 position indices and eight slot indices; there is no duplicate
64-note foundation pattern per voice, heap allocation or extra runtime queue.

Limits inherited from ARP include 64 stored notes and natural shortened-loop
boundary intervals; gates are not clipped at those boundaries. Sparse scales
may offer little or no suitable non-chord color. There are no physical selection
controls or preset changes. Evolution probability controls, phrase changes,
other directions, grooves, rests and MIDI effects are deferred.

Run `sh tests/host/run_motif.sh`; optional `MOTIF_TEST_FLAGS` adds compiler/linker
flags, including AddressSanitizer and UndefinedBehaviorSanitizer. The runner
includes MOTIF, ARP, all scale checks and the complete host engine regression,
including corrected NaN comparisons. The fixed live-material regression uses
explicit LOCK now that unlocked motifs evolve. For the full evolution, runtime
MIDI/undo and regression checks run `sh tests/host/run_motif_evolution.sh`, with
optional `EVOLUTION_TEST_FLAGS` for sanitizers. Neither runner builds firmware.
