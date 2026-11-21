# Tangram musical project record — format 9 (development)

**Status:** the new MIDI-controls branch has source-level format-9 support, but
its build, WSL2 regression, upgrade path and hardware behavior still require
verification. The frozen functional Tangram release candidate used **format 8**.

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

## Named Performance Preset bank (TGPB)

The Performance bank is a distinct CRC-protected flash object with ten
independently named slots (12 ASCII bytes per name). **Format 1 is the only
supported bank format for Tangram 1.0.** Each slot contains an occupied byte,
the name and a 361-byte losslessly compressed project-format-9 record.
The complete bank is 3748 bytes including an eight-byte header; byte 7
records the most recently saved slot, or zero for the default slot 01.

Incompatible development banks are discarded and replaced with the four
factory starters in 01–04 and empty slots 05–10. There is no migration
or compatibility decoder for previous development bank layouts. A valid
format-1 bank is retained on reboot; an empty device constructs the
factory defaults in RAM until the first explicit save. An incompatible
stored bank is replaced through the existing CRC-protected A/B store.

Six snapshot slots and the undo-stack state format are **independent** of
the musical project record and named Performance Preset bank. Their
snapshot wrapper format remains 9.

## Validation

Run `sh tests/run_tangram_midi_controls.sh` and
`sh tests/run_tangram_factory_presets.sh`. Verify the incompatible-bank
reset, subsequent user-save persistence and the actual MIDI output of
STRING TRIO. Build AC79 firmware and perform hardware acceptance before
release. No changes to the project or snapshot storage formats are made
by this recovery branch.
