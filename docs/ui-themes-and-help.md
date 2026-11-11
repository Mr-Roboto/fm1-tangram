# Global themes and on-device help

The themes/help notes originated before the BLE routing extension. On
11 November 2026, Tangram's four GLO pages and wireless BLE-MIDI output to
Korg iM1 on iPad were confirmed on FM-1 hardware. The BLE clock paths and
long-term runtime behavior have not been hardware-verified.

## Appearance

Tap GLO again when already on GLO to cycle
**SETUP → MIDI → APPEARANCE → PERFORMANCE**. SETUP keeps global Root, Sync,
external input (USB/TRS/BLE and combinations), and Lights.
MIDI adds independently selectable Clock Out and MIDI Out routes
(USB, BLE, USB+BLE). APPEARANCE K1 selects a theme without wrapping;
K2 retries a failed global settings save, while K3/K4 remain unused.
PERFORMANCE contains the named performance preset controls.
No additional physical buttons were introduced.

| Theme | Palette |
| --- | --- |
| CLASSIC | Original orange/cyan accents and role colors on pure black |
| GRAPHITE | #5FC9D8 bright cyan / #239BA8 teal accents, #D5E5EA text |
| AMBER | #E96F25 / #F48532 accents, #F5D5AD text |
| NOCTURNE | #A46AFF / #EE57BC accents, #E6D9FF text |
| MONO | #F5F5F5 / #969696 accents, #D0D0D0 text |
| FIRE | #FF343A / #FF7828 accents, #FFE0D2 text |

All six themes use #000000 backgrounds and base surfaces, including panels,
strips and empty steps. Status chips have black rather than tinted backgrounds.
GRAPHITE retains its bright cyan focus and secondary text, with readable
dim text (#B0B0B0), disabled text (#888888), muted labels (#A0A0A0) and restored
teal information/role indicators (#239BA8). Its cyan/teal
controls remain distinct from NOCTURNE's violet/magenta controls. Other palettes
and theme IDs are unchanged by this adjustment; hardware contrast is unverified.
Accent values pass directly through RGB565 truncation; no desaturation is added.
Existing IDs 0..4 remain unchanged; FIRE is appended as ID 5 in format 2.

Every theme retains red source-note markers and grey timing cursors. MONO's
red source marker is its deliberate color exception. State labels, persistent
two-pixel source markers, one-pixel timing cursors, and hollow/dashed suppressed
notes retain their shapes; colors are not the only state cue. Composer current
sections use focus fill; ALGORITHM targets use a focus-accent underline. Stopped
START changes participate in the panel redraw signature. During playback the
underline follows pending selection, or the current section when none is pending.
The appearance page shows role colors and note/cursor examples.

`app/ui_theme.h` owns all operational RGB565 palettes, including surfaces,
text, focus, information/pending, lock, mute, inverse text, suppression, markers
and role colors. No per-theme pattern data or framebuffer was added. Existing
startup logo artwork retains its colors on a pure black background. Platform-owned
update screens remain outside the operational application palette.

Theme edits affect the next display refresh (main band remains capped at 20 Hz).
Header, main band, parameter strip and help include theme in redraw signatures.
Only presentation changes; MIDI queues, generators, density, transport and
musical projects/slots remain untouched by a theme edit.

## Device persistence

Theme is byte 7 of the eight-byte TGDS format-2 record in the existing device
settings object, ID 8, A/B pair `0xCE000/0xCF000`. The flash map, all project,
slot/calibration IDs and firmware-update storage remain unchanged. The platform
retains its CRC and torn-write-safe A/B commit handling.

Seven-byte format-1 settings preserve Sync/Input and import CLASSIC. Missing,
invalid or unsupported records default to INTERNAL/BOTH/CLASSIC. Loading does
not write flash. Theme-only edits do not apply clock settings.

Existing persistence coalesces edits for 250 ms, then writes only when stopped
and MIDI releases are drained. Live appearance changes therefore show PENDING /
STOP until safe to save. Wait for SAVED before power-off. Returning to the saved
choice cancels a needless write. A save failure retains the RAM setting and
shows ERROR; K2 explicitly retries, without an automatic flash retry loop.
Older firmware without a format-2 reader will use its defaults on this record.
See [device-settings.md](device-settings.md).

## Help coverage and navigation

Hold a button alone for one second for its first context-sensitive card. Keep
holding to cycle continuation cards every five seconds; the title shows card
number/count. Release closes help and consumes the held button's usual release
action. A working encoder, key or another button dismisses help using the
existing input rules; these controls retain their normal actions. PLAY still
acts immediately and has no hold-help card: its operation is covered in HOME.
The screen reader announces each new card. Four bounded text lines remain on
screen at a time, with smaller fonts used only where required to fit.

| Hold | Guidance |
| --- | --- |
| HOME | Current QUICK/PLAY/COMPOSE knobs; SELECT, HOME+SELECT tempo, ALGORITHM, PRESETS; MASTER/voice/Composer density; voice taps/holds; harmony, scene, Advance and Tension keys; transport, slots; stage pending/start and ownership |
| ENV | Current TIMING/ROUTING knobs; MIDI channel/register/density/tension; rate, gate, loop, pitch phase; local boundaries and LOCK |
| ARP | Generator/root/scale/direction; ARP/MOTIF and rate/gate location; suppression and both cursor meanings |
| LFO | Read-only lane/status controls; available template scenes via black keys; eight-bar CC morphs |
| SEQ | Groove, loop, pitch phase, direction; independent loops and ARP/MOTIF restrictions |
| FX | Per-voice MIDI delay on/time/feedback/velocity decay; original-note application and retained echo settings |
| GLO | SETUP / MIDI / APPEARANCE / PERFORMANCE; global theme, lights and MIDI routes autosave when safe, stopped-only Sync and output-route edits; Root belongs to the musical project |
| EDIT | Unlocked-voice re-roll and held-voice regeneration at local boundaries |
| REC | B part at next bar; held-voice blank cycle |
| SEL | ENV selected-voice lock and held-voice lock; automatic versus manual edits |
| SAVE | Project/slot persistence; deferred save while playing; SAVE+slot capture and slot-key recall |
| OCT- | Re-roll; HOME+OCT- Undo; octave-button update-mode combination |
| OCT+ | Dice/new seed, live next-bar recall; octave-button update-mode combination |

Slot recall and Undo require running playback to affect the engine; stopped
actions do not queue a recall for the next Play. Dice deliberately replaces
material, including locked source material, using the existing snapshot path;
LOCK protects automatic development and re-roll, not explicit snapshot recall.
These existing behaviors are described by the help rather than changed here.

Button/encoder assignments are derived from `app/ui.c` and its project/runtime
calls; help text lives in `app/ui_help.h`. No musical shortcuts were reassigned.
Current page mappings are in [ui-pages.md](ui-pages.md); Composer stage selection
is described in [composer.md](composer.md). Previously generated word resources
are preserved; only `words.tsv` source is extended for the next authorized build.

Remaining review: compilation/linking, flash/RAM fit, physical RGB565 contrast
and font legibility, persistence after power loss, and screen-reader operation.
