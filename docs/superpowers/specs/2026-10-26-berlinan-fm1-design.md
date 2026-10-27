# Berlinan on the FM-1: design

Date: 2026-10-26. Status: design for review; nothing built yet.

## Intent

Berlinan (`~/Development/Claude/berlin_school_manual`) is the owner's generative MIDI sequencer for macOS in the
Berlin School tradition: a small ensemble of sequencer voices that repeat, phase against each other on unequal loop
lengths, and earn their mutations over time. It needs a laptop.

This project puts Berlinan on the **M-VAVE FM-1** as a firmware, so the sequencer runs on its own box. The FM-1 makes
**no sound**: it is plugged over **USB into a MIDI router**, and each voice goes out on its own MIDI channel to external
synthesizers. The owner plays it from the FM-1's panel: keys, buttons, encoders, the screen.

The firmware must **play exactly what Berlinan on the Mac plays**: the same project and seed, with the same actions at
the same bars, give the same notes and the same CC, event for event, tick for tick.

## Goals

1. A firmware (`.fwsc`) for the FM-1, built on fm1-base, that runs Berlinan's engine and sends its output as MIDI.
2. **Exact equivalence** with Berlinan's engine (notes and CC), proven by tests against the unchanged Swift engine.
3. Play from the panel: the control layout in [Controls](#controls).
4. MIDI clock: INT (the FM-1 is the master, sends clock, Start and Stop over USB) and EXT (it follows incoming MIDI
   clock, from USB or the TRS jack).
5. The state is kept across power-off: SAVE writes it to flash (see [Saving](#saving)).

## Non-goals (v1)

- Moving projects between the Mac and the FM-1 (the owner said it is not needed). The FM-1 starts from Berlinan's
  built-in templates and a seed.
- Melody extraction (♪), promotion to lead, the alternating scheduler, the MIDI delay (echo), slides. None of the
  built-in templates turns them on; they may come in a later version.
- Harmony modes (`Project.harmony`: progression presets, weighted random, bass reharm). All three templates leave it
  `nil`, and the Mac sets it only from its harmony strip, while stopped. The port may leave that code out.
- Editing project fields that the Mac has no live control for (loop length, register, generator, CC lane
  parameters, arranger and B-part settings): shown on the pages, not editable in v1 (see [Pages](#pages)).
- Resuming mid-piece after power-off. As on the Mac, every Play starts the piece from bar 1.
- MIDI out on the TRS jack: the FM-1's TRS jack is input only (`base/include/plat.h`). Bluetooth MIDI is not used.
- Ableton Link, takes and capture, `.mid` export, drag to Live, the hand-edit pencil, LEARN, the auto-updater,
  editing the CC map per synth on the device.
- Any audio from the FM-1.

## Anti-goals

- **Touching Berlinan.** The Mac repo is read-only for this project: no edits, no commits, no new files in it. Code
  and tests are read from it; reference renders are produced by tools that live in this repo.
- "Sounds similar." A port that drifts from the Mac's output, even rarely, has failed, whatever it sounds like.
- `double` in the firmware. fm1-base's build forbids double-precision code.

## Constraints

### Hardware and platform

- M-VAVE FM-1: JieLi AC79 (pi32v2, 360 MHz), 96 KB RAM for `.data`/`.bss`, a 336 KB pool for large buffers, a
  581 KB app slot in flash, a 240×240 ST7789 screen, 27 keys (F3–G5) with LEDs, 14 buttons (FX, SEL, ENV, LFO, EDIT,
  GLO, HOME, SAVE, ARP, SEQ, PLAY, REC, OCT−, OCT+), 7 encoders (KNOB 1–4, SELECT, ALGORITHM, PRESETS), the MASTER
  pot, USB device (MIDI, serial, audio) and a TRS MIDI jack.
- Platform: **fm1-base** (git submodule, with felucca-bsp inside), as in AMB-1 (`~/Development/Claude/fm1-amb`), whose
  layout, build (`build.sh` over `base/tools/build.py`, `app/fm1.toml`), host simulator and tests are the model.
  The app sees only `plat.h`; the same code runs on the FM-1, in the host simulator and in the browser emulator.
- Toolchain (already installed on the owner's Mac): JieLi toolchain at `~/.jieli/toolchain`, AC79 SDK at
  `~/fw-AC79_AIoT_SDK`, Docker, Emscripten, Node.
- No `double` anywhere in the firmware build. 64-bit multiplies must stay out of line (see
  `base/bsp/docs/CPU.md`: inlined 32×32→64 multiplies next to 16-bit loads corrupt a register on this CPU).

### Equivalence

The reference is the **unchanged** Berlinan engine (`BerlinanEngine`, about 4,400 lines of Swift, no Foundation) at
the **pinned commit `0e4a7195cefb9aed5f21ee46bd6eddeebeb776ac`** (v1.2.9), built with the Swift toolchain installed
now (Apple Swift 6.3.3). Moving the pin is a deliberate step: re-port what changed, regenerate the reference files,
rerun the tests. What makes exact equivalence hard:

1. **Randomness goes through the Swift standard library.** The engine has its own generator (`SplitMix64`) and seed
   streams (`SeedStream.derive`, FNV-1a over labels), but draws through `Int.random(in:using:)`,
   `Double.random(in:using:)`, `Bool.random(using:)` and `randomElement(using:)`. The port must reproduce those
   algorithms bit for bit, as Swift 6.3.3 implements them (read them from the Swift sources of that version and
   verify against the engine, not from memory).
2. **Decisions go through `Double`.** Probabilities (`< 0.3`, `< 0.2`, `< 0.15`), gate widths (`Tick(Double(stepTicks) * gate.width)`), the
   density macro, ramps (`Double(tick - startTick) / Double(endTick - startTick)`), the tempo where the engine uses it
   (ambient mode: `project.bpm / Double(beatsPerBar)`), CC lanes and scenes. The port must reproduce IEEE-754 double
   results exactly without the `double` type: exact integer reformulations where they are provably equal, and a
   software double (bit-exact IEEE add, subtract, multiply, divide, compare, conversions) elsewhere. Either way the
   tests decide.
3. **`sin()` in CC lanes.** Apple's libm `sin` is not specified bit for bit. The port's sine must give the same CC
   value (after the engine's scaling and rounding to 0–127) for every case the tests render; where the reference
   value sits on a rounding boundary, the port must match Apple's result.
4. **Dictionaries and sets.** The engine sorts dictionary keys where order matters (`weights.keys.sorted()`); the port
   must follow the engine's actual iteration order everywhere, including `Set` uses (`Generators.swift`,
   `Arrangement.swift`).

Equivalence is defined at the **engine's output**: the notes (`RenderedNote`: tick, pitch, velocity, gate, channel)
and CC (`RenderedCC`) that `LiveEngine.render(range:)` and `renderCC(range:)` produce, for a project, a seed and a
script of actions enqueued at given ticks. The runtime layer on the Mac (`Transport`: mute, silencing on stop, the
note registry, clock) is ported by behaviour and tested on its own.

## What the FM-1 plays

- **Templates:** Berlinan's three built-ins, as the Mac app instantiates them: `Project.demo` ("Demo trio"),
  `Project.fullEnsemble` ("Full ensemble") and `Project.ambientDrift` ("Ambient drift"), each moved to **A minor**
  (`TemplateStore.defaultKey`: root 9, natural minor), as `TemplateStore.inDefaultKey` does. They are compiled into the
  firmware.
- **Seed:** a template gets a fresh master seed when it is loaded, as on the Mac. The seed is shown (Setup page), so a
  run can be reproduced on the Mac.
- **Voices:** up to seven, by role: Pluck 1, Pluck 2, Bass, Pad, Lead, Drone, Heartbeat. Each on the MIDI channel the
  template gives it (changeable on the Voice page).
- **Output:** USB MIDI (to the router) only.

## Saving

As a Berlinan document on the Mac (`AppServices.saveDocument`): SAVE writes the project (`Project`: the template, the
seed, key and mode, tempo, density, arranger settings, voice channels) and the six snapshot slots
(`LiveEngine.StateSnapshot`) to flash. The undo stack and the running engine state are not saved. After power-on the
saved project is loaded and Play starts it from bar 1, as every Play does on the Mac. Flash store objects hold at
most 3,840 bytes each (`PLAT_STORE_MAX`); the format is the builder's.

## Controls

The layout was proposed and accepted "to try, to be changed later". It follows the FM-1's panel and AMB-1's
conventions (a lit button shows the page; holding a button for a second shows a help card).

### Keys (27), three zones

| Zone | Keys | Function | LED |
| --- | --- | --- | --- |
| Voices | F3, F#3, G3, G#3, A3, A#3, B3 | Pluck 1, Pluck 2, Bass, Pad, Lead, Drone, Heartbeat. Tap: mute / unmute (the Mac's transport mute). Tap on a voice the arranger holds back (WAITS): bring it in (`setVoiceActive(on: true)`, the Mac's Voice On/Off). Hold: select the voice (and modify the next button: see below) | lit: playing; blinking: WAITS; dark: muted; off: not in the template |
| Harmony | C4–B4 white | Degrees 1–7: diatonic transposition at the next bar (`transpose(degreeOffset: n−1)`) | the current degree lit, a pending one blinking |
| | C#4, D#4, F#4, G#4, A#4 | CC scenes 1–5: morph toward the template's scene n (in its order) over 8 bars (`morphToScene(name:bars: 8)`). Keys past the template's scenes do nothing (Ambient drift has two, the others none); templates are not given extra scenes | the template's scenes dim, the active one lit |
| Form | C5, C#5, D5, D#5, E5, F5 | Snapshot slots 1–6: tap recalls at the next bar; SAVE + slot stores | stored slots lit |
| | F#5 | `>>`: the arranger's next change at the next bar (`advancePhase`) | |
| | G5 | T·n: spend one unit of tension (`spendTension`, on the pad voice, else the first voice, as the Mac does) | lit while some is left |

### Buttons

| Button | Function |
| --- | --- |
| PLAY | Play / stop (INT: also MIDI Start/Stop and clock) |
| REC | B-part (`fireBPart`). With a voice key held: blank cycle for that voice |
| EDIT | Re-roll every unlocked voice (as the Mac's "Re-roll all unlocked"). With a voice key held: regenerate that voice |
| SEL | With a voice key held (or on the Voice page): lock / unlock the voice |
| SAVE | Save the state to flash. SAVE + a slot key: store a snapshot in that slot |
| OCT− | Undo (the Mac's undo stack: 20 states, pushed before every destructive action) |
| OCT+ | Dice: new master seed, swapped in at the next bar while playing (as `AppServices.reseed`) |
| HOME, ENV, ARP, LFO, SEQ, FX, GLO | Pages (below) |

### Encoders and the pot

| Control | Function |
| --- | --- |
| MASTER | Density (DENS), 0–100 %: the FM-1 makes no sound, so the volume pot is a physical density fader. Binds at the next bar |
| SELECT | The selected voice. HOME held + SELECT: tempo |
| ALGORITHM | Scale mode (as the Mac's key/mode choice) |
| PRESETS | Template; while playing it takes over at the next bar, as on the Mac (Berlinan 1.2.3) |
| KNOB 1–4 | The four values of the page, shown at the bottom of the screen |

### Pages

| Button | Page | KNOB 1–4 |
| --- | --- | --- |
| HOME | Perform: the voice lanes (name, channel, pattern strip with playhead, loop length, legitimization count, lock / mute / WAITS), the HORIZON (what is coming: pending degree, B-part countdown, arranger phase, nearest mutation), convergence (LCM in bars), tension left | tempo · arranger AUTO on/off · form (classic arc / slow build / ambient plateau) · ambient mode |
| ENV | Voice (the selected one) | MIDI channel · loop length · register (octave) · generator |
| ARP | Pattern: the selected voice's pattern in steps, its generation and legitimization count | Ratchet (added 2026-10-27, see [Decided](#decided)) · the rest shown (melody controls: later versions) |
| LFO | CC and modulation | lane target CC · cycle (bars) · depth · scene |
| SEQ | Arranger | phase · phase length · B-part bed drop · reprise |
| FX | Delay cheat sheet: delay times for the owner's own effects at the current tempo (as the Mac's Delay Cheat Sheet). The MIDI echo is a later version | — |
| GLO | Setup | root · sync INT/EXT · EXT clock input (USB, TRS, both) · lights; the seed is shown |

**Editable in v1 is only what the Mac can change**, through the same engine action or project change, and with the
same binding (next bar while playing, or stopped only) as on the Mac: tempo, arranger AUTO, form and ambient mode,
key root and mode, the voice's MIDI channel, sync, density, and the keys' and buttons' actions above. The other page
values (loop length, register, generator, CC lane parameters, arranger phase settings) are **shown, not
editable**; their knobs do nothing in v1. A later version may make them editable while stopped (edit the project,
start again from bar 1), with the Swift engine rendering the edited project as the reference.

## Testing

- **Reference renders, produced in this repo.** A small Swift tool in this repo (for example `tools/reference/`)
  depends on `BerlinanEngine` by local path, read-only, and writes the engine's output for a project, a seed and an
  action script (notes and CC, with ticks). It also writes the three templates (in A minor) as the port sees them.
  `berlinan-cli render` is the reference for offline renders. The tool builds against the pinned commit (for example a
  detached worktree outside the Berlinan repo, or a pinned local path); its renders are **committed as fixed
  reference files**, so the tests do not need Swift and an upstream change cannot shift them silently.
- **The matrix:** the three templates × a set of seeds × action scripts (degrees, Dice, Re-roll, regenerate, lock,
  blank cycle, B-part, tension, scenes, slots and undo, arranger on/off and `>>`, density, key and mode changes,
  template switch while playing), each long enough to cross mutations, B-parts and the arranger's phases (hundreds of
  bars). The FM-1 code in the host simulator must produce the same events, byte for byte.
- **Unit tests** for the hard parts on their own: SplitMix64 and seed streams, Swift's random algorithms, the
  software double against the host's own `double` (on the host only), the sine.
- **The runtime:** host simulator scenarios (as AMB-1's `tests/scenarios/*.amb`) for mute, stop silencing every
  sounding note, MIDI clock out, following EXT clock, SAVE and load, the panel mapping, screenshots of each page.
- **The firmware build:** `./build.sh` passes fm1-base's checks (no `double`, sizes, register access).
- Tests run with `sh tests/run_tests.sh`, as in AMB-1.

## Decided

- Platform fm1-base, not M-VAVE's FM1 Open (`~/Development/Claude/fm1_open`): proven by AMB-1, has the host simulator
  the equivalence tests need, and the browser emulator.
- Repository: `~/Development/Claude/fm1-berlinan`, local git only (nothing is pushed), fm1-base as a submodule,
  laid out like AMB-1 (`app/`, `app/fm1.toml`, `tests/`, `docs/`).
- Licence: GPL-2.0-or-later, as Berlinan, since the C engine is a translation of it. fm1-base (MPL-2.0) allows it.
- Language: C, in the style of AMB-1 and fm1-base (their comment density, naming and idiom; files read as if the
  same author wrote them).
- No changes to Berlinan.
- (Owner) SAVE as a Mac document: project + slots, play from bar 1. Page values without a Mac control: shown only.
- (Owner, 2026-10-27) One exception: the Pattern page's KNOB 1, "Ratchet", sets `Project.legitimization`'s
  `ratchetProbability`, 0–50 % in steps of 5, OFF (0) by default (the Mac has no control and plays nil, 0.15).
  Saved in the project (format 2; a format-1 record loads with it OFF). No live engine action exists on the Mac, so
  a change while playing reaches the next Play (as the stale project's other changes) and the screen says AT NEXT
  PLAY. Proven by the script line `ratchet K` (`tests/equiv/README.md`).
- (Designer's calls, open to change) Reference pinned to Berlinan `0e4a719` with committed reference files; harmony
  modes out of v1; scene keys use only the template's own scenes; a tap on a WAITS voice brings it in; MIDI out on
  USB only, TRS as a clock input.

## Left to the builder

- How the engine is split into C files, its data structures and memory layout (pool versus static RAM), within the
  FM-1's limits: seven voices, six snapshot slots, twenty undo states.
- Where to use exact integer reformulations and where the software double; the sine implementation.
- The scheduling on the device: which platform callback drives the sequencer (the audio interrupt as a clock, or
  `app_tick`), the render window size, the MIDI clock out timing, following EXT clock.
- The flash format of the saved state.
- The screen design within the page contents above, following AMB-1's look.
- The order of work, as long as equivalence is proven before the panel is built on top of it.
