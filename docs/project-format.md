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

The bank remains a separate CRC-protected persistent flash object with
**ten independently named slots** of at most twelve ASCII bytes each.
The v2 bank header byte 7 is an optional last-saved slot selector: 0 means
legacy/default slot 01, 1..10 means the last committed named SAVE. This
changes only the selected GLO PERFORMANCE slot at boot; it does not
load a working preset or write flash while browsing. The v2 header prefix
(first seven bytes) remains unchanged and old v2 banks with zero are valid.

| Bank version | Embedded project | Entry (occupied+name+record) | Entire bank |
| --- | --- | --- | --- |
| TGPB 1 | Project format 8, 312 B | 325 B | 3258 B |
| TGPB 2 | Project format 9 compressed losslessly to 361 B | 374 B | 3748 B |

The v2 bank bit-packs each voice's 10-byte LFO/mixer extension into 7 bytes
without losing its 0–127 values or four-bit active mask. The bank holds
the same 312-byte project prefix plus 49 encoded bytes for seven voices.
Full project saves continue to use the uncompressed 382-byte format.

Both bank sizes include the 8-byte header. The format-2 bank fits the
platform's 3840-byte per-object storage allocation (subject to verified
platform headers/CRC overhead in the actual firmware build).

An older TGPB-1 bank is decoded and migrated **in RAM** at boot, preserving
occupied names and projects. Loading existing banks performs **no flash
write**; a subsequent explicit user SAVE or RENAME commits the complete
TGPB-2 bank. Corrupt banks are not overwritten silently.

Six snapshot slots and the undo-stack state format are **independent** of
the musical project record and named Performance Preset bank. Their
snapshot wrapper format remains 9.

## Upgrade acceptance still required

Run `sh tests/run_tangram_midi_controls.sh` on WSL2. The targeted host
scenario includes project SAVE/reboot, MIDI CC output routing and a host-only
TGPB-1 fixture migrated with no boot-time rewrite. Before installation
verify actual firmware RAM/flash capacity, compatibility with existing
saved projects and slot banks, and an on-device real-MIDI smoke test.

**Do not publish/flash on the basis of this document or the present source
commits alone.**
