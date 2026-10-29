# Per-voice MIDI note delay — TGM-004

**NOT TESTED.** No tests, sanitizers, validation scripts, builds or hardware
verification were run. This is a device-independent MIDI output effect.

`bl_vcfg_t.delay` is a `bl_delay_t` with four byte-sized settings:

| Field | Values | Default |
| --- | --- | --- |
| `enabled` | 0 OFF, 1 ON | OFF |
| `time` | 1..7 sixteenth-note units, in duration order below | 3 |
| `feedback` | 25, 50, 75, 100 percent → 1, 2, 3, 4 echoes | 75 |
| `decay` | Integer percent, 50..100 | 70 |

| TIME | Musical duration | Ticks at 960 PPQN |
| --- | --- | ---: |
| 1 | 1/16 | 240 |
| 2 | 1/8 | 480 |
| 3 | Dotted 1/8 (3/16) | 720 |
| 4 | 1/4 (4/16) | 960 |
| 5 | 5/16 | 1200 |
| 6 | Dotted 1/4 (6/16) | 1440 |
| 7 | 7/16 | 1680 |

```c
voice.delay = (bl_delay_t){1, 3, 75, 70};
```

Omitted/zero timing, feedback and decay fields normalize to their defaults;
out-of-range timing/decay or non-discrete feedback does likewise. Nonzero
`enabled` normalizes to ON. Saved records require normalized valid values.
The persistent app project also has `proj.delay[voice_index]`; programmatic
changes can be applied through the existing project apply/load lifecycle.
There are no physical controls or new live-control actions.

For each original emitted hit, echo `i` (1..feedback/25) is placed at
`original_tick + i * time * 240`. Velocity is
`round(original_velocity * (decay / 100)^i)`, with positive half-up rounding.
Integer numerator/denominator arithmetic avoids accumulating earlier rounding:
velocity 100 at 70% gives 70, 49, 34; at 50% gives 50, 25, 13; at 100% it stays
100. A result rounded to zero is skipped before the MIDI 1..127 clamp.
The source velocity includes existing accent/humanization processing. Ratchet
hits are original emitted hits and each receives its own echoes with its actual
sub-gate duration. Echoes never enter the generator or produce further echoes.

Every queued echo already contains its final pitch, channel, velocity, onset
and paired end. Later regeneration, harmony or delay-setting changes cannot
rewrite it. Pitch/channel/gate are copied from the original emitted hit. RATE,
GATE, grooves, directions, phasing, patterns and MOTIF evolution are untouched.
Musical tick scheduling carries echoes across loop boundaries and follows both
internal tempo changes and the existing external MIDI clock.

Before any note-on on an already-held channel/pitch, the transport sends an
explicit note-off for the previous strike. The registry records the new owning
voice and scheduled end tick. A queued off is sent only when its tick matches
that currently held strike's end; older offs cannot cut it short. If an old and
new off share the same tick, releasing at that tick is correct for both.
Collisions across voices use this same registry. Other pitches/channels continue
independently. MIDI has one current owner for a colliding channel/pitch: muting
an earlier owner does not release a later voice's retrigger.

STOP drops all timed events, including echoes, and marks every Tangram-held key
for explicit release. MUTE purges that voice's queued originals and echoes on
all channels and marks its currently owned keys for release. Channel changes
retain the existing purge behavior for that voice's old channel. No channel-wide
All Notes Off is used, avoiding collateral releases of other voices/pitches.
The interrupt drains marked keys before new timed notes, within the existing
16-packet/ms budget. Duplicate release requests do not add duplicate events or
overflow the immediate queue. `rt_idle` waits for releases and the existing drain
delay before allowing storage writes. LOCK does not disable this output effect.

Delay settings are part of each materialized voice, so existing capture/recall
and undo copy them. Packed state adds four bytes per voice for all generators.
The app project saves four bytes per available voice. Project format is **3**;
snapshot slot format is **5**. Older formats are not migrated. Pending MIDI tails
are transient transport state: snapshots preserve settings, not a rewind of
already scheduled echoes; recall leaves existing tails with their original data.

The existing 1024-event sorted queue is reused. On/off pairs reserve two entries
atomically; overflow drops the complete pair and increments `rt.dropped`.
Dense material, long gates and four-echo tails can exhaust that capacity.
Each event adds one end tick; the registry adds 2048 end ticks, 2048 voice-owner
bytes and two 16-bit flush counters. On a typical ABI the event ring sizes grow
by eight bytes per entry (1024 timed + 256 immediate); actual device padding and
resource headroom remain unverified. There are no extra event queues, heap
allocations, recursive feedback, audio processing or device-specific messages.

All output is subject to the existing USB/MIDI throughput limit. A retrigger
requires two packet-budget slots; if only one remains it waits for the next
interrupt. Forced retriggers shorten overlapping same-key durations as required;
different keys remain polyphonic. No runtime, host or hardware behavior has been
verified in this task.
