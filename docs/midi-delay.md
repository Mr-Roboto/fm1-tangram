# Per-voice MIDI note delay — TGM-004 / TGM-005A-CORE3

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
The persistent app project also has `proj.delay[voice_index]`. CORE3 adds live
`proj_get_delay`, `proj_set_delay`, and `proj_set_delay_field` (template voice
indices), with corresponding `rt_*` interfaces addressed by `BL_VID_`.
Field selectors are `BL_DELAY_ENABLED`, `BL_DELAY_TIME`, `BL_DELAY_FEEDBACK`
and `BL_DELAY_DECAY`. Explicit setters reject invalid values, including 0%
feedback; OFF changes only `enabled`, preserving the selected feedback.
Runtime/app delay edits are refused while a project replacement is pending.
Callers may use existing `undo_push` before an edit; setters do not create undo
points themselves. No physical controls or encoder bindings are added.

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
that currently held strike's end and owning voice; older offs cannot cut it short. If an old and
new off share the same tick, releasing at that tick is correct for both.
Collisions across voices use this same registry. Other pitches/channels continue
independently. MIDI has one current owner for a colliding channel/pitch: muting
an earlier owner does not release a later voice's retrigger.

STOP drops all timed events, including echoes, and marks every Tangram-held key
for explicit release. MUTE purges that voice's queued originals and echoes on
all channels and marks its currently owned keys for release. Channel changes
update unsent originals and future source frames without purging or releasing
existing notes or echoes. Their already captured offs keep the original channel.
No channel-wide
All Notes Off is used, avoiding collateral releases of other voices/pitches.
The interrupt drains marked keys before new timed notes, within the existing
16-packet/ms budget. Duplicate release requests do not add duplicate events or
overflow the immediate queue. `rt_idle` waits for releases and the existing drain
delay before allowing storage writes. LOCK does not disable this output effect.

Delay settings and MIDI channel are part of each materialized voice, so existing
capture/recall and undo copy them. CORE3 adds one channel byte per packed voice;
the four delay bytes were already present. The app project already saves both
settings, so project format remains **5**; snapshot slot format advances **7→8**.
Older formats are not migrated. Pending MIDI tails
are transient transport state: snapshots preserve settings, not a rewind of
already scheduled echoes; recall leaves existing tails with their original data.

The existing 1024-event sorted queue is reused. Lookahead inserts a tagged
original with prospective delay/channel values copied from that rendered note's
voice frame. Main-loop live setters scan the bounded queue under the existing
lock and update **only unsent originals**. Thus edits affect the next actually
triggered original even when it was prepared earlier; they cannot rewrite any
echo or an active note's off. Source pitch, velocity, duration and musical tick
remain unchanged. Returning from snapshots also supplies the restored output
frame to subsequently prepared originals, including tension notes.

When an original reaches dispatch, it is removed from the queue only after
reserving its wire packet budget. The freed slot immediately holds its concrete
note-off, before its note-on is sent. Its applicable delay settings are then
captured and the echo pairs are inserted. Only tagged originals produce echoes.
Echo pairs reserve two slots atomically; overflow drops the complete pair and
increments `rt.dropped`. A refused original never sounds. Original offs cannot
lose their reserved slot to optional echoes, preventing overflow-created orphans.
Dense material, long gates and four-echo tails can still exhaust queue capacity.

Echo timing uses the original event's musical tick as the base; interrupt/USB
delivery jitter does not introduce a wall-clock delay interval. OFF suppresses
new echoes at the next original while existing echoes continue. Notes already
sent, and echoes already queued, keep their captured channel, pitch, onset,
velocity and duration when live controls, regeneration or snapshot recall change
the source settings.

Event metadata adds a kind tag and four prospective delay bytes. Rendered notes
carry their source frame's delay/channel; no new queue, heap allocation or
unbounded structure is introduced. On a typical ABI an event grows from 24 to
32 bytes (1024 timed + 256 immediate entries), but device padding, memory
headroom and worst-case interrupt cost remain unverified. Echo insertion now
runs at dispatch; dense/full queues can increase interrupt processing time.
There is no recursive feedback, audio processing or device-specific message.

All output is subject to the existing USB/MIDI throughput limit. A retrigger
requires two packet-budget slots; if only one remains it waits for the next
interrupt. Forced retriggers shorten overlapping same-key durations as required;
different keys remain polyphonic. No runtime, host or hardware behavior has been
verified in this task.
