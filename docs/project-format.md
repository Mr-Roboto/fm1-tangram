# Musical project format 6

**NOT TESTED.** Projects now use explicit little-endian field serialization in
`app/project_record.c`, rather than writing the bytes of `proj_t`. The in-memory
structure no longer has `sync` or `ext_ports`; no replacement bytes are reserved.
New saves write format 6 only. Device clock settings live in their own object.

For seven voices, the format-6 record is exactly 301 bytes:

| Fields in serialized order | Bytes |
| --- | ---: |
| Magic, format (u32 each) | 8 |
| Template, root, scale, lights (u8 each) | 4 |
| Seed (u64), internal BPM (i32) | 12 |
| Arrangement present/enabled/ambient/reprise (u8 each), min/max (i16 each) | 8 |
| Density percent, density present (u8 each) | 2 |
| Seven channels, ratchet percent (u8 each) | 8 |
| Seven delay settings: enabled/time/feedback/decay (u8 each) | 28 |
| Seven explicit-configuration flags (u8 each) | 7 |
| Seven voice configurations (32 bytes each) | 224 |

Each voice configuration stores root i32; scale mode/count u8 and 12 signed
offsets; generator u8, a/b i16, direction/groove u8; length u32; register low/high
and pitch PHASE u8. Even unconfigured entries retain their defined field values.
No C padding is written. Active patterns, pending lifecycle and traversal state
continue to use the existing snapshot/undo mechanism, not this project record.

The format-5 reader recognizes the historical 336-byte, little-endian native
layout with seven voices. Clock bytes 11/12 are consumed and ignored; lights is
byte 13, seed byte 16. It skips historical alignment bytes 14/15 and 81..83.
The configuration array begins at byte 84 with seven 36-byte entries: root at
entry+0, scale at +4, generator at +20, a/b at +22/+24, direction/groove at
+26/+27, length at +28, registers/PHASE at +32..34. Padding +18/+19, +21 and +35
is ignored. These offsets do not depend on the current compiler's layout.

Readers reject truncation, trailing bytes, unknown formats/magic and invalid
musical settings. Accepted format-5 data becomes format 6 in memory and on the
next explicit SAVE; loading does not rewrite flash or import old clock choices.
Other historical project versions are unsupported. A hypothetical format-5
record from a different ABI/byte order is not inferred or guessed.

Removing the two clock bytes and all 33 historical padding bytes accounts for
the reduction from 336 to 301. Object IDs/flash allocation are unchanged from
GLOBAL1, and the snapshot slot wrapper remains format 8 with identical payloads.
