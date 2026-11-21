# Tangram 1.0 — four musical factory starter presets (candidate)

**Status:** clean candidate based on hardware-confirmed 4074514; test/build and musical acceptance pending.

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

## Preset-local MIDI register bounds

| Preset | MIDI voice ranges in template order |
|---|---|
| BERLIN SEQ | 60–91, 60–91, 36–60 |
| AMBIENT DRFT | 36–60, 55–84, 60–84, 48–72 |
| MINIMAL PULS | 60–88, 64–91, 36–60, 48–72, 60–96, 36–47 |
| PHASE GLASS | 60–84, 60–84, 36–60 |

These are proposed musical settings, not changes to the template defaults. The original reference C, generator and fold logic are unchanged. Drum notes are absolute and not transposed.

## Storage and validation

One final Performance bank format (1); incompatible development banks discarded; factory slots 01–04, empty 05–10. Subsequent explicit user saves persist. No migration, no changes to working project or snapshot storage, no automatic preset load at startup. Run the MIDI-controls and factory-preset suites and AC79 build; compare the three fresh templates with the hardware-confirmed baseline before final acceptance.
