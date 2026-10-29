# ARP and MOTIF playback directions — TGM-003A

**NOT TESTED.** No tests, sanitizers, validation scripts, builds or hardware
verification were run for this implementation.

Each voice selects `gen.direction`, independently of `a` (RATE), `b` (GATE),
`phase_steps`, harmony and MIDI channel. Omitted/zero selects `BL_DIR_UP`;
invalid factory configuration falls back to UP. For example:

```c
voice.gen = (bl_gencfg_t){BL_GEN_MOTIF, 240, 73, BL_DIR_M_PENDULUM};
```

For four source tones, numbered by their source order rather than MIDI pitch:

| Configuration | Playback order |
| --- | --- |
| `BL_DIR_UP` | 1 2 3 4 1 2 3 4 ... |
| `BL_DIR_DOWN` | 4 3 2 1 4 3 2 1 ... |
| `BL_DIR_M_DOWN` | 1 4 3 2 1 4 3 2 ... |
| `BL_DIR_PENDULUM` | 1 2 3 4 3 2 1 2 3 4 ... |
| `BL_DIR_M_PENDULUM` | 1 4 3 2 1 2 3 4 3 2 ... |

ARP's source is its eight-tone, two-octave chord cycle (or the available prefix
for shorter patterns). MOTIF's source is the complete stored melodic phrase,
including its repeated chord cycles and any color tones in either half. Source
order is preserved; there is no sorting by resolved/folded MIDI note. Existing
UP playback uses the original stored pitches at their original onset slots,
including its existing local-loop repetition behavior.

Other directions carry a bounded cursor through local loop boundaries rather
than restarting there. A 15-onset loop advances that cursor by 15 and a 16-onset
loop by 16, independently for each voice. Eighth-rate loops instead advance by
their actual stored onset count. Pendulum periods are `2 * (source_count - 1)`;
turnaround endpoints are visited once. M-PENDULUM has a one-time initial root
pickup, followed by descending-first pendulum traversal. A single-tone source
always selects that tone. Equal pitches already present in the source are not
removed; the traversal adds no duplicate endpoint visits.

Only pitch selection changes. Gates, note positions, velocities, accents,
ratchets, loop duration and harmony scheduling remain tied to the original
onset grid. The live renderer maps pitches into its existing scratch voice
before density filtering, so removed onsets do not renumber the source sequence.
The shortened-loop boundary interval and existing gate-overlap behavior remain;
there is no inserted onset, stretching or loop resynchronization.

MOTIF generation and evolution continue to operate on the original ascending
source pattern. Root/midpoint protection, substitution budget, nonadjacency,
neighbor constraints and the deterministic evolution schedule remain source
constraints. Reordered playback does not promise an audible root at every local
loop start or an ascending audible contour. Evolution updates the source at its
existing local boundaries without restarting direction traversal.

LOCK freezes mutation/evolution and blocks regeneration, while pitch traversal
continues during playback. Blank loops advance the traversal grid; inactive
voices do not render or advance. Reactivation retains the cursor while using
the existing local-loop restart. Manual regeneration retains the selected
direction, restarts its traversal and resets MOTIF's evolution lifecycle.

Snapshots and undo store the direction and cursor, plus local tick phase and
active/blank state for both ARP and MOTIF. Recall resumes that traversal and
phase through the existing recall queue. Project slots use format 3 with no
legacy migration. Packed records add two bytes per MOTIF voice relative to
TGM-002B, and seven per ARP voice (direction/cursor plus phase/flags). Other
generator records are unchanged. Invalid direction/cursor metadata is rejected.

Runtime state adds two `uint8_t` fields per pattern; configuration adds one
`uint8_t` per generator config, with ABI-dependent padding. Existing snapshot
phase/flag arrays and the existing render scratch voice are reused. There are
no extra full patterns, queues, heap allocations or schedulers.

Selection is programmatic; no presets, physical controls, pages, grooves or
additional mutation behavior are introduced. The existing 64-note capacity
applies. Additional directions require extending the enum and traversal mapping.
