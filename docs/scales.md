# Tangram scales — TGM-001A

Tangram provides 23 preset scales, each available with roots C through B (0–11).
Intervals are ascending semitones above the root; their number is the degree count.
Existing IDs 0–9, their intervals and raw names remain unchanged. New enum names
use the `BL_MODE_` prefix with the labels below; their raw and UI names are those labels.

| ID | Scale | Intervals |
| --- | --- | --- |
| 0 | Natural Minor | 0,2,3,5,7,8,10 |
| 1 | Dorian | 0,2,3,5,7,9,10 |
| 2 | Phrygian | 0,1,3,5,7,8,10 |
| 3 | Mixolydian | 0,2,4,5,7,9,10 |
| 4 | Lydian | 0,2,4,6,7,9,11 |
| 5 | Lydian Dominant (Berlinan) | 0,2,4,6,7,9,10 |
| 6 | Harmonic Minor | 0,2,3,5,7,8,11 |
| 7 | Minor Pentatonic | 0,3,5,7,10 |
| 8 | Locrian | 0,1,3,5,6,8,10 |
| 9 | Major | 0,2,4,5,7,9,11 |
| 10 | PMAJ — Major Pentatonic | 0,2,4,7,9 |
| 11 | EGYP — Egyptian Pentatonic | 0,2,5,7,10 |
| 12 | HIRA — Hirajoshi | 0,2,3,7,8 |
| 13 | MELM — Melodic Minor | 0,2,3,5,7,9,11 |
| 14 | HMAJ — Harmonic Major | 0,2,4,5,7,8,11 |
| 15 | ALTR — Altered | 0,1,3,4,6,8,10 |
| 16 | GYPS — Hungarian Minor | 0,2,3,6,7,8,11 |
| 17 | ROMN — Romanian Minor | 0,2,3,6,7,9,10 |
| 18 | BHAI — Bhairav Thaat | 0,1,4,5,7,8,11 |
| 19 | MARV — Marva Thaat | 0,1,4,6,7,9,11 |
| 20 | PURV — Purvi Thaat | 0,1,4,6,7,8,11 |
| 21 | TODI — Todi Thaat | 0,1,3,6,7,8,11 |
| 22 | ARAB — Arabic | 0,2,4,5,6,8,10 |

The musical intervals and names follow the 22-scale tables in X0X-RMX
[`tb3po.c`](https://github.com/Mr-Roboto/fm1-x0x-rmx/blob/dev/independent-firmware-identity/firmware/src/seq/tb3po.c)
(blob `0ddbba4685806b8c719638cbb8c8c1accb31dade`) and
[`tb3po.h`](https://github.com/Mr-Roboto/fm1-x0x-rmx/blob/dev/independent-firmware-identity/firmware/src/seq/tb3po.h)
on `dev/independent-firmware-identity`, plus Berlinan's Lydian Dominant.
Tangram retains its own ID order. The reference is GPL-3.0-only; only musical
interval/name data was used, without copying generator code or license boilerplate.
Existing Tangram source licensing remains GPL-2.0-or-later.

Tangram is an independent greenfield firmware project: compatibility with Berlinan
projects, historical firmware or previously saved formats is not required. This task
adds no migration or storage changes. The existing `BL_NMODES` bounds in project
validation and key selection now accept 0–22 and reject 23 and above. The default
remains A natural minor.

TGM-001A changes only scale availability. Degree resolution, nearest-tone
quantization (ties down), UI pages, ALGORITHM's circular one-step selection per
nonzero input read, generators, arranger, transport and MIDI scheduling are unchanged.
`bl_scale_preset` continues to require a valid mode ID; project selection and loading
validate IDs before lookup.

Run `sh tests/host/run_scales.sh` for focused host checks (all scales/roots,
quantization, IDs/names/table coverage, current project settings and storage,
ALGORITHM cycling, and the original music regression). Optional
`SCALE_TEST_FLAGS` enables sanitizer checks. No legacy compatibility tests,
firmware builds, hardware tests or release packaging are included.
