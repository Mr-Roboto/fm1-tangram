# Tangram 1.0 — UX pass (development only)

**Date:** 11 November 2026  
**Functional freeze:** `dev/tangram-ble-midi-spike` at `5b0f7d7a9814ba7d90cda3e953a192137199d85b`  
**UX branch:** `dev/tangram-ux-1.0`  
**Release:** still **1.0**, not 1.1 or 1.2; there has been no public 1.0 release yet.

## Decided constraints

- No new musical, MIDI, clock, generator, Composer or persistence functionality.
- Preserve the HOME musical overview and its performance flow.
- Preserve last-visited subpage memory for each hardware section; never force return to the first page.
- Preserve the existing Performance Preset editor, including its four-encoder name entry.
- Preserve GLO APPEARANCE and its global save/retry status. Do not redistribute these unrelated functions across pages.
- No additional SHIFT key, modifier mode, or new tempo shortcut without a deliberate layout review.
- The existing HOME + SELECT tempo shortcut and contextual ALGORITHM behavior remain unchanged for this first pass.
- Keep existing transient status messages in addition to the new persistent active/queued value distinction.
- Keep the current help hold time and advance cadence for now; clarify the card counter instead.

## Current navigation facts

- **Four GLO subpages:** SETUP → MIDI → APPEARANCE → PERFORMANCE.
- **Seven GLO help cards:** one context-dependent opening card plus six further help cards. `GLO HELP 1/7` is not a screen/subpage counter.
- HOME: three subpages; ENV: two; all other sections: one each.
- ALGORITHM: musical scale/mode outside HOME, Composer stage while on HOME COMPOSE, inactive on HOME QUICK and HOME PLAY. The tempo shortcut uses HOME + SELECT.

## UX pilot changes on this branch

1. Label help overlay `<button> HELP x/y`; repair obsolete GLO navigation instructions and the inaccurate warning that Composer START is not saved.
2. Compare **sounding active** versus **staged target** settings in the knob strip while playing, without changing the engine or data model. Normal theme focus marker = active value; bright target marker and target text = queued value. For circular knobs the queued position is a short radial tick ON the same ring (not an external dot); for pips the queued position is the brighter pip. If equal, only the active marker remains. Text-only controls receive a small bright pending dot. Unsupported (N/A) controls never show a phantom queued marker.
3. Add simulator assertions for actual subpage/help counts and two active/queued Composer encoder changes: K4 Evolution and K3 Phrase Bars (a real circular ring, AUTO -> 16 through four detents; both NEXT BAR).
4. Label an enabled Composer's implicit Classic Arc form as `CLASSIC`, not `NONE` or `DEFAULT`. No arranger setting is silently written; this displays the actual fallback musical form.
5. On HOME COMPOSE, split `CYCLE` and `NEXT` into distinct columns while preserving the existing font sizes, section strip, counters and musical HOME overview.

**UX phase 1 checkpoint — accepted for layout/design (11 November 2026):**
The user ran the full `sh tests/run_tangram_ux.sh` host smoke gate,
which passed, and reviewed the generated `composer_queued.png`,
`composer_applied.png`, `bars_queued.png` and `bars_applied.png` views.
The dual active-versus-queued positions on both pips and circular knobs
were accepted, as were the CLASSIC fallback label and separate
CYCLE/NEXT layout; the existing font sizes are retained. Help-card counts
are clarified. This completes the **UX phase 1 design/simulator checkpoint**,
not the full hardware UX acceptance or a release. No subsequent
layout/encoder reorganization is permitted on this phase-1 branch.

**Functional code freeze:** the original BLE/Composer acceptance branch
is unchanged. **UX phase 2** (reallocating duplicate knob assignments)
must proceed on a separate child branch, only after the accepted
phase-1 checkpoint is committed. The UX pilot has not been reflashed
and visually validated on the actual FM-1; this is intentionally
tracked as an outstanding integration step rather than called done.

## Layout duplication audit — next coherent revision

The following are genuine overlapping editor controls in the current firmware; they are **not changed by the first marker/help pilot**:

| Function | Current multiple locations |
| --- | --- |
| Generator, per-voice Root, Scale | HOME QUICK and ARP HARMONY |
| Direction | HOME PLAY, ARP HARMONY, SEQ MOTION |
| Groove | HOME PLAY and SEQ MOTION |
| Loop length, Pitch phase | ENV TIMING and SEQ MOTION |
| Delay on/off and Delay time | HOME PLAY and FX MIDI DELAY |
| Composer Evolution/Tension | HOME COMPOSE versus ENV ROUTING (related controls split up) |

The reallocation must be reviewed as **one consistent four-encoder information architecture**, not an arbitrary swap. In particular: do not make ARP/SEQ buttons useless merely to remove duplication; do not strip time-critical HOME shortcuts; do not introduce hidden secondary modes. The user will supply additional hands-on findings before the final mapping and the UX freeze.

## Focused verification

From the UX branch in WSL2:

```sh
sh tests/run_tangram_ux.sh
```

This includes the unchanged functional smoke gate, the UX simulator scenario
`tests/scenarios/ux_pending_help.bln`, and four visual PNGs under
`app/build/scenarios/ux_pending_help/` for inspection: `composer_queued.png`,
`composer_applied.png`, `bars_queued.png`, and `bars_applied.png`.

**No reflash or hardware changes are needed to run these host tests.**
