# ARP/MOTIF playback grooves — TGM-003B

**NOT TESTED.** No tests, sanitizers, validation scripts, builds or hardware
verification were run for this implementation.

Each voice configures `gen.groove` independently of RATE (`a`), GATE (`b`),
direction and `phase_steps`. Zero/omitted selects `BL_GROOVE_STRAIGHT`, retaining
every existing onset and accent. Invalid factory values fall back to STRAIGHT.
For example:

```c
voice.gen = (bl_gencfg_t){BL_GEN_MOTIF, 240, 73,
                         BL_DIR_PENDULUM, BL_GROOVE_BROKEN_8};
```

Positions below are numbered from 1 at the local pattern start. "Even sixteenth
positions" means zero-based indices 0, 2, 4, ... (one-based 1, 3, 5, ...).

| Groove | RATE 1/8 | RATE 1/16 | Added accents |
| --- | --- | --- | --- |
| `BL_GROOVE_STRAIGHT` | All positions | All positions | None |
| `BL_GROOVE_PULSE_8` | All eighth positions | Every second sixteenth | Eighth positions 1 and 5 |
| `BL_GROOVE_DRIVE_16` | All eighth positions | All sixteenth positions | Eighth positions 1, 3, 5, 7; sixteenth positions 1, 5, 9, 13 |
| `BL_GROOVE_BROKEN_8` | Eight-slot mask directly | Mask on zero-based even sixteenths; intervening positions silent | Active mask slots 1, 4, 8 |

BROKEN 8's mask is `PLAY REST PLAY PLAY REST PLAY REST PLAY`. PULSE 8 and
BROKEN 8 use 480-tick slots; DRIVE 16 accents every 960 ticks. Masks/accents
repeat every 3840 ticks within a long local pattern, and restart at each local
pattern start. A 3600-tick loop stops at its actual end and starts its own next
mask there, independently of global bars. Nothing fills a missing final slot or
corrects the natural boundary interval. RATE is never changed automatically.

Groove filters rendered occurrences and supplements source accents; it does not
remove or edit stored notes. A suppressed onset produces no MIDI note event.
Already scheduled note-offs use the existing transport unchanged. Sounding
notes retain their gates, source velocity and ratchets; the existing velocity
policy also receives groove accents. Density filtering can further remove
onsets and retains its existing source-accent ranking.

All five directions still traverse every underlying grid position, including
suppressed positions. Groove phase uses the source onset's local tick position,
not its selected pitch, direction cursor, compacted index or global bar.
MOTIF generation, protected source anchors, evolution boundaries, RNG streams
and cycle counting are unchanged. A protected source tone can be silent under
a mask. LOCK does not disable the selected groove or traversal; it retains its
existing mutation/evolution and regeneration behavior.

Manual regeneration retains the groove selection. Snapshots/undo capture it
alongside direction, local phase and existing MOTIF evolution metadata. One
`uint8_t` is added to generator configuration and pattern metadata; ABI padding
may affect structure sizes. Packed snapshots add one byte per ARP/MOTIF voice;
other generator records are unchanged. Slot format is 4, with no legacy migration.
Invalid saved groove identifiers are rejected. No groove cursor, extra pattern,
runtime queue or scheduler is added.

Selection is programmatic and applies to ARP/MOTIF only. Existing presets retain
STRAIGHT. There are no physical controls, new presets, gate/rhythm mutation or
additional groove modes. Existing note capacity and gate-overlap limitations
remain. Compilation and runtime behavior have not been verified.
