# Tangram 1.0 — final release gate (11 November 2026)

**Status:** user has deliberately deferred the first public 1.0 release. This
is a *release-planning record*, not authorization to modify the music engine,
flash layout or the already accepted UX phase-1 baseline.

## What is already accepted

- Tangram is a MIDI-only multivoice composition/performance engine; there is
  no internal audio synthesis and no planned internal sound engine.
- Functional/BLE/Composer baseline remains frozen on
  `dev/tangram-ble-midi-spike` at `5b0f7d7`.
- UX phase 1 active-versus-queued ring/pips, help counts and Composer
  readouts are accepted on `dev/tangram-ux-1.0` at `c66fead`.
- UX phase 2 keeps both ENV pages, renaming only `ENV ROUTING` to
  `ENV VOICE`. ARP HARMONY and SEQ MOTION explicitly remain as-is.
- The user has enjoyed a Philip Glass/minimal-music sound with an Angels
  Choir, Tuba/Flügel and Double Reed external sound palette. This is an
  example of the sound-source-independent design, not a firmware patch.
- No public 1.0 release until three or four musical Performance Presets
  have been composed, reviewed and safely packaged.

## Outstanding minimum tasks before public 1.0

1. **Musical content.** The user will author 3–4 original Performance
   Presets on hardware during the next few days. This is not a task for
   firmware code to invent. The bank has 10 slots with 12 printable ASCII
   characters per preset name; keep names at or under 12 bytes.
2. **Optional working-Preset INIT (clarified).** The user's INIT idea is
   for resetting the **current performance configuration** to a fresh
   initialized working preset that can subsequently overwrite any of
   the ten occupied slots. It was NOT a request to delete an individual
   stored slot. The existing GLO PERFORMANCE operations remain LOAD / SAVE /
   RENAME; there is no user INIT command yet. Do not add a destructive
   slot-delete or slot-reset action on the basis of this discussion.
   Treat an eventual INIT workflow as a separate, optional UX decision.
3. **Factory provisioning.** The existing `TGPB` bank is stored as a
   separate persistent flash object. Merely storing four slots on the
   developer's FM-1 does **not** embed them in a downloadable firmware.
   Provide a reproducible, tested way to include the agreed factory
   music in a clean-device release. Never blindly rewrite the existing
   preset bank on firmware upgrades. Test clean install, upgrade with
   existing user presets, overwrite/rename, stop/idle restrictions,
   preservation of slots and project on failed writes, and format
   compatibility. Do not assume a firmware update always preserves flash
   objects; verify the real installer and device path.
4. **Final UX/build/device gate.** Re-run
   `sh tests/run_tangram_ux_layout.sh`, compile a full AC79 firmware,
   inspect final size/RAM budgets, install on FM-1 and perform hands-on
   smoke tests for performance slot operations, BLE/USB routing, next-bar
   control overlays, Composer FORM, normal SAVE, and interrupted-write
   resilience where testable without jeopardizing hardware.
5. **Release materials.** Publish only after the bank and the actual
   firmware build have been verified. Include version/firmware identity,
   supported features, clear MIDI channel and performance-preset usage,
   update/preservation caveats, and download/checksum. No speculative
   1.1 work in the 1.0 release branch.

## Out of scope

- Reorganizing ARP HARMONY, SEQ MOTION, ENV TIMING, HOME QUICK/PLAY/COMPOSE,
  or the Performance Preset name editor.
- New musical generators, more drums, audio synthesis, or further Composer
  behavior changes. Minor observed defects can be logged for a possible
  1.1 *after* 1.0; 1.1 is not yet under development.
- Shipping copyrighted musical compositions as presets; the musical presets
  will be the user's own configurations.

**Current implementation status:** GLASSFACE is the chosen first preset name, but its musical configuration and the other planned presets have not yet been embedded as factory content. INIT is optional and absent. The LFO edit / FX MIXER exception is being developed separately on `dev/tangram-1.0-midi-controls`; no public 1.0 release has occurred.
