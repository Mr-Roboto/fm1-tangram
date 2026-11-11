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
2. **Explicit `INIT` action.** The current GLO PERFORMANCE operation list
   in `app/ui.c` is strictly `LOAD / SAVE / RENAME`; it does **not**
   contain `INIT`. `performance_presets_init()` in
   `app/performance_presets.c` is *internal startup bank loading* and
   must not be confused with a selectable slot operation. The requested
   fourth action should be implemented only with precise, tested per-slot
   semantics, a confirmation before destructive writes, stopped/idle
   gating, and preservation of every other slot and the current working
   project. Define whether INIT restores a named factory performance or
   a neutral initialized preset before implementing either behavior;
   these two meanings are not interchangeable.
3. **Factory provisioning.** The existing `TGPB` bank is stored as a
   separate persistent flash object. Merely storing four slots on the
   developer's FM-1 does **not** embed them in a downloadable firmware.
   Provide a reproducible, tested way to include the agreed factory
   music in a clean-device release. Never blindly rewrite the existing
   preset bank on firmware upgrades. Test clean install, upgrade with
   existing user presets, overwrite/rename/INIT, stop/idle restrictions,
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

**Current implementation status:** planning only; INIT UI, factory preset
seeding, and 3–4 musical presets have **not** been implemented by this
document.
