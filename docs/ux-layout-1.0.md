# Tangram 1.0 – UX layout phase 2

## Baselines and decisions

- Frozen functional base: `dev/tangram-ble-midi-spike` at `5b0f7d7`.
- Completed UX phase 1: `dev/tangram-ux-1.0` at `c66fead`.
- This branch: `dev/tangram-ux-layout-1.0`.
- Release version remains **1.0**, not 1.2.
- **Approved and implemented**: relabel the second ENV subpage from
  `ENV ROUTING` to **`ENV VOICE`**, including main UI header, context help
  and current documentation. The `ENV TIMING` page stays. Both pages keep
  the same four original knob bindings, shown below.
- **Rejected**: the earlier proposal to delete ENV TIMING and move its
  rate/gate/loop/phase controls wholesale to SEQ MOTION. This did not improve
  the conceptual information architecture.
- **Protected**: HOME musical overview and controls, Performance Presets,
  GLO settings/appearance, last-visited subpage behavior, no new SHIFT
  modifier. No musical engine, clock, preset format or save changes.

## Current layout (confirmed from app/ui.c)

| Page | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| HOME QUICK | Track | Generator | Root | Scale |
| HOME PLAY | Direction | Groove | Delay | Delay Time |
| HOME COMPOSE | Composer | Form | Phrase Bars | Evolution |
| ENV TIMING | Rate | Gate | Loop Length | Pitch Phase |
| **ENV VOICE** | MIDI Channel | Register | Voice Density | Composer Tension |
| ARP HARMONY | Generator | Root | Scale | Direction |
| SEQ MOTION | Groove | Loop Length | Pitch Phase | Direction |
| FX MIDI DELAY | Delay | Time | Feedback | Decay |

All mappings beyond the ENV VOICE label remain as they were at the end
of UX phase 1.

## ARP HARMONY – factual audit

- K1 Generator picks the generator family of the selected voice. This is
  the same editor as HOME QUICK K2, and is not restricted to the ARP generator.
- K2 Voice Root and K3 Scale duplicate HOME QUICK K3/K4. These determine
  the selected melodic voice's pitch system, not a global setting.
- K4 Direction duplicates HOME PLAY K1 and SEQ MOTION K4. It is meaningful
  for ARP/MOTIF; the implementation renders non-ARP/MOTIF cases as N/A.
- Heartbeat has no editable per-voice root or scale on this page.
- This page currently offers no unique edit parameter compared with HOME
  QUICK / PLAY. Whether that is a useful dedicated workflow or wasteful
  redundancy is a UX decision, not a code bug.

## SEQ MOTION – factual audit

- K1 Groove also appears on HOME PLAY K2; applies to ARP/MOTIF.
- K2 Loop Length also appears on ENV TIMING K3.
- K3 Pitch Phase also appears on ENV TIMING K4; it offsets pitch-order,
  not the timing/clock phase.
- K4 Direction also appears on HOME PLAY K1 and ARP HARMONY K4.
- The SEQ content panel reuses the same selected-voice pattern grid
  as ARP. These sections are distinguished primarily by their encoder
  assignments, not by independent sequencing engines or a second grid.
- The generator's exact supported edits differ: ARP/MOTIF expose
  direction/groove; other generators may display N/A for these.

## Next layout decision – no code change yet

Review the *musical purpose* of ARP HARMONY and SEQ MOTION separately,
including their page visuals and their overlap with HOME QUICK/PLAY and
ENV TIMING. Decide which overlapping parameters are **intentional
performance shortcuts** and which consume scarce encoder slots without
benefit. Only then agree on a complete K1–K4 mapping for each page;
do not swap Rate/Gate for Groove/Direction simply to eliminate a page.

Composer Tension remains on ENV VOICE for now. Its eventual position is a
separate decision; do not move it without a complete Composer-control plan.

## Verification

Use `sh tests/run_tangram_ux_layout.sh` after pulling the phase-2 branch.
This first runs all existing phase-1 and functional host checks, then asserts:

- ENV still has two subpages;
- ENV TIMING has the unchanged Rate/Gate/Loop/Phase bindings;
- ENV VOICE has the unchanged Channel/Register/Density/Tension bindings;
- tapping ENV cycles TIMING → VOICE → TIMING;
- the new title is `ENV VOICE`.

The UX layout rename has **not** yet been rebuilt/flashed on the hardware
and has **not** yet passed the new host test until the WSL2 result is supplied.
