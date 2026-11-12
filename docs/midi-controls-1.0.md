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
- MIDI wire values remain 0–127. The PAN *display* and ring are bipolar: CC64 reads 0 at center, CC0 reads -64 and CC127 reads +64. Because MIDI has 128 values and this signed labeling has 129 integer positions, one intermediate positive label is skipped by rounding. Volume starts at 100; cutoff displays 70; resonance displays 30. These defaults are readouts, **not automatically transmitted** until a knob is moved or explicitly saved values are restored on PLAY.
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

## Hardware observations and UX correction (12 November 2026)

User hardware feedback on the installed first MIDI-controls build: the LFO target/cycle/depth/center controls and the second FX MIXER page are operational; the target synth (Korg iM1) visibly receives MIDI CC, but no confirmed parameter response. The iM1's Control Change MIDI Filter **ON blocks CC**; its global and per-timbre filtering need checking before attributing the absence of sound changes to Tangram. This is not an accepted synthesis-side CC response test.

The subsequent source revision (not yet tested/built/flashed) changes ONLY the mixer initial display values to 100 / CC64-centered 0 / 70 / 30, renders PAN as a bipolar ring, and orders LFO choices FILTER -> RESONANCE -> DECAY without changing stored format-9 target IDs. The ten-slot bank v2 now stores the **last successfully saved slot number** in its previously reserved header byte (0=legacy default; 1..10=last stored). GLO PERFORMANCE restores only that slot *selection* at boot, never silently loads it. Browsing and boot incur no new flash writes. The physical SAVE LED still indicates deferred working-project SAVE; the selected named Performance Preset is confirmed using the existing blinking SELECT LED after required name entry.

## Implementation checkpoint (host acceptance from previous build)

The development branch now contains: new LFO K1–K4 editors, the second FX MIXER page, immediate channel CC output (0–127), per-project persisted values, project-record format 9 (382 bytes), 3748-byte losslessly packed ten-slot bank v2 (49-byte extension per preset), backward readers for project format 8 and bank v1, and a new host regression scenario covering simulated CC messages, project SAVE/reboot and legacy bank migration.

**Host acceptance (12 November 2026, user-confirmed):** `sh tests/run_tangram_midi_controls.sh` completed successfully on WSL2. Its focused regressions covered BLE MIDI routing, Composer FORM, UX staged markers and ENV VOICE, LFO edits, CC7/10/74/71 MIDI output, musical-project SAVE/reboot, GLASSFACE named-slot save/load, and a test-only legacy bank v1 migration into the 3,748-byte compact v2 bank. Project format 9 is 382 bytes. This acceptance is **host simulation only**, not an AC79 device build or a hardware test.

**Device build acceptance (12 November 2026, user-confirmed):** `./build.sh` completed successfully on WSL2 for the JieLi/AC79 target: image **356,880 bytes**, RAM **89,672 / 98,304** bytes (8,632 free), pool **335,832 / 344,064** bytes (8,232 free). The loader image and package were generated: `app/build/tangram.fwsc` **610,108 bytes**, development identity `FM-1_97500`. The build passed the BSP-only register-access, forbidden inlined 64-bit multiplication and SoftFloat routine checks. The locally modified `base` submodule is intentional and was left untouched.

**NOT YET VERIFIED:** on-device output and UX, long-running LFO/scene interactions, real on-device flash update/recovery, MIDI routing under full load and flash persistence. Previously rendered CC events can remain queued briefly after an LFO target change. The actual firmware *now passes its build*; perform a cautious hardware acceptance before calling it release-ready. The development identity `FM-1_97500` is **not** the final 1.0 release.

## Release gate

Do not flash until a verified host build + existing regression smoke
suite + new controller persistence/live-output tests have passed.
Check actual AC79 firmware RAM and image budgets before hardware QA.
Factory preset creation, optional INIT and the final 1.0 release
remain separate acceptance steps.
