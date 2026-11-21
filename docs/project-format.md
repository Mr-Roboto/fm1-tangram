# Tangram musical project record — format 9 (development)

**Status:** Tangram 1.0 release candidate, 21 November 2026.

## Current format 9

The serializer in `app/project_record.c` writes an explicit, byte-packed
little-endian record with no native C padding. For seven voices:

| Region | Bytes |
| --- | ---: |
| Original project-format 8 record | 312 |
| LFO and MIDI CC mixer extension (7 voices × 10 bytes) | 70 |
| **Total project-format 9** | **382** |

The first 312 bytes retain the format-8 layout. Only the format version
at header byte 4 changes from 8 to 9. Version 8 had already repurposed the
historical LIGHTS byte to persist the **Composer start section**; global
device settings and output routes are not project fields.

The appended data is **ten bytes per voice**, repeated in template voice
order (up to seven slots): LFO override flag, LFO CC target selector, LFO
cycle selector, LFO depth percent, LFO center percent, mixer active mask,
then the four CC values (Volume CC7, Pan CC10, Cutoff CC74, Resonance CC71).
Each is a u8. The LFO cycle index maps to 0.5/1/2/4/8/16/32/64 bars.
The mixer mask makes an untouched controller *unset*; displaying a
default value must not send a MIDI message until it is touched or an
explicitly stored mixer value is applied on PLAY.

Readers remain compatible with the previous formats 5 (336 bytes),
6 (301 bytes), 7 and 8 (both 312 bytes). Older accepted records initialize
the ten new bytes per voice to zero. After validation, the in-memory
project is upgraded to format 9; no automatic flash write occurs on load.
Explicit SAVE writes format 9. The record decoder rejects truncation,
extra trailing bytes, unsupported versions and invalid field ranges.
Older firmware is not assumed capable of reading newer saved records.

## Named Performance Preset bank (TGPP)

The only supported Performance bank format is **1**. A separate CRC-protected
flash object holds an eight-byte header and ten 374-byte entries (occupied
byte, 12-byte ASCII name, 361-byte losslessly packed format-9 project).
Total: 3748 bytes. Header byte 7 selects the last explicitly saved slot.
The previous development signature is rejected; incompatible development
banks are replaced with four factory starters and six empty slots. No
migration is performed. A valid bank is retained across reboot; a blank
device constructs factory defaults in RAM until the first explicit save.
The working project and snapshot storage are not modified by this reset.

Six snapshot slots and the undo-stack state format are **independent** of
the musical project record and named Performance Preset bank. Their
snapshot wrapper format remains 9.

## Validation

Run both focused MIDI-controls and factory-preset suites before the release
build. Hardware behavior was confirmed by the user at `2418898` on 21 November
2026. The generated release package and web simulator require final checks.
