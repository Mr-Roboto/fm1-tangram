# Tangram 1.0 — four musical factory starter presets (candidate)

**Status:** Recovery candidate. Host tests, AC79 build and hardware acceptance pending.

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

## Storage

There is one Performance Bank format: **1**. Incompatible development
banks are discarded. Four factory starters occupy slots 01–04; slots
05–10 are empty. Subsequent explicit SAVE and RENAME edits persist.
There is no migration of previous bank formats. Global settings, the
working project and snapshots remain separate.

## Pitch recovery

Global +12-semitone changes have been removed. PLUCK 1's default upper
register limit is MIDI 84 rather than 79, so the A-root (69) and its octave
(81) are not folded into the same note. Other voices and explicit user
register choices are unchanged. ENV displays the most recent original
MIDI Note-On number; the pattern strip uses the effective voice key,
reference and register fold. The focused host scenario captures the wire
output of both plucks and checks that both octave notes actually occur.

Validation: run the MIDI-controls suite, the factory-preset suite and the
AC79 build, followed by user-confirmed hardware acceptance. This branch
is not a release.
