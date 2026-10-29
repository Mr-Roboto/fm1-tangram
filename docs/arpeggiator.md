# Tangram ARP core — TGM-001B

`BL_GEN_ARP` is appended as generator ID 7; IDs 0–6 are unchanged.
It uses the existing pattern factory, degree pitches, live engine, harmony,
register folding, occurrences, MIDI output and snapshots. No presets change.

## Configuration

```c
voice.gen = (bl_gencfg_t){BL_GEN_ARP, 480, 0}; /* eighths, default gate */
voice.phase_steps = -1;                     /* optional 15-step loop */
```

The existing `int16_t` fields represent independent timing quantities:

| Field | Meaning | Defaults / invalid values |
| --- | --- | --- |
| `a` | RATE onset spacing: 480 ticks (1/8) or 240 ticks (1/16) | Zero or any other value selects 480 |
| `b` | GATE duration in ticks, strictly positive and less than spacing | Zero selects half spacing; negative or `>= spacing` also falls back to half spacing |

Thus default gates are 240 and 120 ticks. An explicit gate of 73 ticks stays
73 at either RATE. Gates do not determine note count or pattern duration.
These are programmatic fields, with no new physical control or page.
`direction` now selects the [five playback directions](directions.md);
zero retains UP. Direction does not change either timing field.
`groove` selects the [playback groove](grooves.md); zero retains STRAIGHT.

## Material and duration

The factory starts with 3840 ticks (one 4/4 bar at 960 PPQN), independent of RATE
or pitch-cycle length. The live factory applies the existing per-voice
`phase_steps` in units of 240 ticks and fills that duration's grid. Zero means
16 steps; -1 means 15 steps; other valid adjustments work independently per voice.
There is one onset at each grid position strictly before the end.
`bl_with_length` still removes notes at or beyond a shortened end.

`bl_make_arp` accepts an explicit duration for this integration, restricted to
240..INT32_MAX ticks (the engine's existing minimum snapshot length and documented
pattern arithmetic range); an invalid duration falls back to one bar. Up to
`BL_MAX_NOTES` (64) onsets are stored; excess onsets are omitted and counted in
the factory return value, as with existing generators. The live factory currently
does not surface omitted counts. For a completely occupied long loop, keep its
onset count within that capacity.

Chord selection fixes the root, then chooses an unused scale degree nearest
each of these semitone roles, in order: third in [3,4], fifth at 7, seventh in
[10,11]. Distance is zero inside the interval; equal distances choose the lower
semitone. Selected degrees are sorted before playback. This yields four distinct
in-scale tones, including useful substitutes for pentatonic and unusual scales:
Locrian/Altered use 0,3,6,10; Hirajoshi uses 0,3,7,8; Arabic uses 0,4,6,10.
Malformed scales (fewer than four or more than twelve tones, missing root,
nonascending offsets, or offsets outside 0–11) produce an empty ARP pattern.

UP expands those tones across two octaves by adding `scale.count` degrees,
then repeats that eight-note pitch cycle across the available onsets. A minor
is A–C–E–G–A–C–E–G, with the second group one octave higher. Material is
deterministic regardless of role or seed; the existing rendering velocity policy
still applies. Live key changes retain existing degree-resolution semantics;
manual regeneration recomputes chord selection for the current key.

## Loops and stability

Occurrences and loop repeats are unchanged. A 3600-tick loop at eighth RATE has
onsets 0,480,...,3360, then 3600 at the next loop's root: the boundary interval
is **240 ticks**, naturally shorter than the interior 480 ticks. Default gate
240 abuts that next onset. Explicit gates longer than the boundary interval can
cross it, even though they are shorter than RATE spacing; very short phase-adjusted
loops have the same consideration. No gate clipping, forced onset, stretching,
boundary correction or resynchronization is applied. Musical boundary treatment
is deferred. UP retains the existing pattern-loop pitch restart; the other
directions carry their traversal cursor across local boundaries.

ARP is excluded from Legitimizer automatic mutation, without setting LOCK.
Its normal repeat lifecycle counts down, but reaching established does not mutate
it or increment its generation. Manual regeneration still runs at its loop
boundary and may recreate identical notes; explicit LOCK still blocks it.
Existing arranger directives and density processing retain their behavior.

The conceptual reference is Berlinan's
[`Generators.swift` / `arpeggiate()`](https://github.com/jiripisa/berlinan/blob/main/Sources/BerlinanEngine/Generators.swift)
(reviewed blob `4153b744ad0298a7e214bd1dbafce6b64d43f473`). Its sorted octave expansion
is retained conceptually; its pitch-cycle-derived duration is deliberately replaced
with independent duration and onset spacing. Existing source licensing is retained.
Tangram is greenfield: no historical project/state migration is added.

MOTIF and the five directions are described in their dedicated notes.
Playback groove masks are documented separately. Deferred: independent pitch/gate
cycles, progression logic, MIDI delay/effects, new presets, length controls and
physical generator selection (TGM-005).

Run `sh tests/host/run_arp.sh` for ARP checks, TGM-001A scale tests and the
original engine regression cases. `ARP_TEST_FLAGS` adds compiler/linker flags
(for example AddressSanitizer and UndefinedBehaviorSanitizer). These are host
tests only; no firmware, flashing or hardware validation is performed.
