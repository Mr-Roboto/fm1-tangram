# Tangram 1.0 — LFO edit and FX controller extension (work in progress)

**Branch:** `dev/tangram-1.0-midi-controls` from approved UX phase 2.
The frozen musical/BLE baseline and completed UX branches must not be
modified. This is an explicit *pre-1.0 feature exception*, not version 1.1.

## Approved goal

- Keep HOME, ARP HARMONY, SEQ MOTION and both ENV pages intact.
- Make LFO controls editable for the selected voice even when its
  original template has no LFO (NONE). The scenes shown on the page
  remain the existing keyboard-controlled morphs, not a knob-selectable
  LFO parameter. Use K1 target, K2 cycle, K3 depth and K4 center.
- Put a second page behind FX MIDI DELAY:
  **FX MIXER**: K1 CC7 Volume, K2 CC10 Pan, K3 CC74 Brightness
  (commonly filter cutoff), K4 CC71 Resonance. Actual synth response to CC71 depends on its MIDI implementation. Select the voice using SELECT.
- MIDI CCs are channel-scoped. Several Tangram voices mapped to a
  shared MIDI channel cannot have independent CC volumes/filters.
  A downstream synth may ignore or remap any optional sound CC.
- Provide 0–127 parameter values; PAN center 64.
- Keep working-project saving and ten Performance Presets, including
  optional backward reading of old-format banks, rather than adding transient-only
  controls that disappear when powered off.
- Do not automatically overwrite users' banks or flash new factory
  content. GLASSFACE is the agreed first candidate name (9 ASCII
  characters); the user authors the musical presets.
- The user's optional INIT request means initializing a **working
  performance configuration**, not necessarily erasing an individual
  stored bank slot. It is not an approved slot-delete action and is
  deliberately separate from this MIDI controller change.

## Critical implementation constraints

- CC lanes are currently template-specified and read-only, and a voice
  may have no lane; introducing an editable LFO requires a per-voice
  override and an ability to create a lane for NONE voices.
- Existing template scenes use CC-map index 0, often CC74; editor
  overrides must not destroy their source map or scene bindings.
- The existing project record is 312 bytes (format 8); the ten-slot
  `TGPB` bank format 1 embeds ten such records. Any persisted new
  fields require both backward-compatible record decoding **and**
  migration of stored banks without data loss. Respect the 3840-byte
  flash-object limit and keep old banks readable.
- A live controller change must not restart the music engine, force a
  project takeover or end held notes. Route through the existing
  notes/CC output port policy; never transmit to a different port
  than normal MIDI output.
- CC74 from a mixer knob may be overridden by an active LFO on CC74
  or a template scene morph. Do not claim independent filtering.

## Implementation checkpoint (not yet tested)

The development branch now contains: new LFO K1–K4 editors, the second FX MIXER page, immediate channel CC output (0–127), per-project persisted values, project-record format 9 (382 bytes), 3748-byte losslessly packed ten-slot bank v2 (49-byte extension per preset), backward readers for project format 8 and bank v1, and a new host regression scenario covering simulated CC messages, project SAVE/reboot and legacy bank migration.

**Host acceptance (12 November 2026, user-confirmed):** `sh tests/run_tangram_midi_controls.sh` completed successfully on WSL2. Its focused regressions covered BLE MIDI routing, Composer FORM, UX staged markers and ENV VOICE, LFO edits, CC7/10/74/71 MIDI output, musical-project SAVE/reboot, GLASSFACE named-slot save/load, and a test-only legacy bank v1 migration into the 3,748-byte compact v2 bank. Project format 9 is 382 bytes. This acceptance is **host simulation only**, not an AC79 device build or a hardware test.

**NOT YET VERIFIED:** full JieLi/AC79 firmware build, actual device output, long-term scene interactions, real on-device flash update/recovery and hardware RAM/image limits. Previously rendered CC events can remain queued briefly after an LFO target change. Do not flash this branch until the device build passes and the hardware acceptance is planned.

## Release gate

Do not flash until a verified host build + existing regression smoke
suite + new controller persistence/live-output tests have passed.
Check actual AC79 firmware RAM and image budgets before hardware QA.
Factory preset creation, optional INIT and the final 1.0 release
remain separate acceptance steps.
