# Tangram 1.0 — four musical factory starter presets (candidate)

**Status:** implemented on isolated branch `feature/tangram-1.0-factory-presets`; NOT yet compiled, tested or hardware accepted. No release/tag/flash is implied.

The four names and original stylistic directions were agreed during the Tangram 2 sessions. Specific parameters not already fixed are new musical defaults proposed for 1.0. External synthesizer patches remain outside Tangram's saved Performance Preset records.

## The four starters

| Slot | Name | Template | Tempo / key | Form / Composer | Global density |
|---|---|---|---|---|---|
| 01 | BERLIN SEQ | STRING TRIO | 110 BPM / A natural minor | SLOW BUILD / ON, GENTLE | 85% |
| 02 | AMBIENT DRFT | AMBIENT DRIFT | 72 BPM / D Dorian | AMBIENT PLATEAU / ON, GENTLE | 62% |
| 03 | MINIMAL PULS | FULL ENSEMBLE | 115 BPM / E natural minor | CLASSIC ARC / ON, MOVING | 80% |
| 04 | PHASE GLASS | STRING TRIO | 112 BPM / C major | Composer OFF, Arranger OFF (free phasing) | 90% |

Each starter uses a fixed, distinct seed to enable reproducible material. Slots 05–10 remain available to the user. Midi channel assignments follow the templates; devices and their sound programs must be selected externally.

### 01 BERLIN SEQ
- PLUCK 1: ARP, 16 sixteenth-note steps, gate 135 ticks, UP, voice density 90%.
- PLUCK 2: MOTIF, 15 sixteenth-note steps, gate 150 ticks, PENDULUM and pitch phase 2, voice density 78%.
- BASS: inherited pulsing bass (voice density 90%) to anchor the changing melodic relationship.
- SLOW BUILD and gentle Composer evolution provide an extended Berlin School arc.

### 02 AMBIENT DRFT
- DRONE: inherited sustained generator, full voice density.
- PLUCK 1: inherited seeded figure at 35% voice density.
- PLUCK 2: 15-step MOTIF at eighth-note rate, slow pendulum, 35% density. One MIDI delay at quarter-note time with 25% feedback and 75% decay.
- PAD: inherited long-layer generator at 90% voice density.
- Spacious arrangement is primarily produced by slow timing and global density. Existing template CC scenes remain available. CC modulation depends on the external synth's mapping.

### 03 MINIMAL PULS
- PLUCK 1: 16-step MOTIF in eighths, 78% voice density.
- PLUCK 2: 15-step ARP in sixteenths with M-PENDULUM and PULSE 8 groove, 52%.
- BASS: inherited bass melody at 85%; PAD at 38%, LEAD at 30%.
- HEARTBEAT: kick/snare/closed hi-hat, SPARSE groove, 75% density, original GM channel 10.
- Subtle 5% ratchet and MOVING Composer variation for gradual rhythmic change.

### 04 PHASE GLASS
- PLUCK 1: 16-step ARP, 1/16, UP, 95% density.
- PLUCK 2: 15-step MOTIF, 1/16, PENDULUM, pitch phase 3, 82%.
- BASS: 13-step ARP, 1/16, DOWN, pitch phase 1, 55%.
- Neither Composer nor Arranger changes the form: phase shifts follow the independent loop lengths. No MIDI delay is imposed, preserving note timing.

## Data and upgrade behavior

The four factory starters are ordinary, editable format-9 Performance Preset records in the existing ten-slot TGPB-v2 bank. No project-format or flash-map revision is intended.

- On initialization, **only vacant** slots 1–4 get an in-memory factory starter. Any existing occupied user entry takes precedence.
- Valid saved TGPB-v1 or v2 banks are read first, without overwriting flash.
- Boot **does not write flash**; a later explicit user SAVE/RENAME persists the bank, including the remaining factory starters.
- Loading a factory starter requires stopped/idle transport, as with any Performance Preset.
- Device-global routing, BLE, clock setup, display settings, snapshot slots and external synth patches are unchanged.
- No automatic loading of factory slot contents is added. The separate existing startup behavior is left untouched.

## Validation gate before 1.0

1. Review the fixed preset parameters and confirm all four factory records survive format-9 serialize/compact/unpack/read roundtrips.
2. Run **only the focused host smoke**: `sh tests/run_tangram_factory_presets.sh` (builds host simulator only; no AC79 firmware or hardware flashing).
3. Confirm that existing user entries in slots 1–4 are never replaced, v1/v2 bank migrations retain names, and boot performs no flash writes.
4. After explicit agreement, build AC79 firmware for this branch, check CPU/RAM/flash size, and load onto a real FM-1 for a short musical MIDI/USB/BLE acceptance.
5. Only after those passes: decide how to integrate into the designated 1.0 release branch, verify identity/version/release artefacts and publish deliberately. Do **not** overwrite historical gh-pages (older Berlinan site) during this work.

No full-suite tests, releases, merges, firmware builds or deployments are performed by the source-change commits.
