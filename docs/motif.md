# Stable MOTIF — TGM-002A

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

MOTIF repeats unchanged and bypasses automatic Legitimizer mutation without
setting LOCK. Manual regeneration uses the existing new seed and current key
at the local loop boundary; explicit LOCK blocks it. Snapshot/undo actions and
existing arranger/density behavior are retained. No global harmony changes,
loop-boundary correction or new compatibility layer is introduced.

Limits inherited from ARP include 64 stored notes and natural shortened-loop
boundary intervals; gates are not clipped at those boundaries. Sparse scales
may offer little or no suitable non-chord color. There are no physical selection
controls or preset changes. Automatic evolution, partial mutation probabilities,
phrase changes (TGM-002B), other directions, grooves, rests and MIDI effects are
deferred.

Run `sh tests/host/run_motif.sh`; optional `MOTIF_TEST_FLAGS` adds compiler/linker
flags, including AddressSanitizer and UndefinedBehaviorSanitizer. The runner
includes MOTIF, ARP, all scale checks and the complete host engine regression,
including corrected NaN comparisons. It performs no firmware/hardware build.
