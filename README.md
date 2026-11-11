![FM-Tangram logo](app/site/assets/tangram-logo.svg)

# FM-Tangram

**Generative MIDI Sequencer & Composer for the M-VAVE FM-1**

*Berlin School · Ambient · Minimalism · Evolving electronic music*

FM-Tangram is a standalone, performance-oriented MIDI sequencer and generative composition environment for the **M-VAVE FM-1**. It combines independently evolving voices, musical patterns, rhythmic variation and a multi-stage Composer to create long-form electronic performances directly on the hardware — without a computer in the performance workflow.

**Tangram is MIDI-only.** It generates no audio and has no internal synthesizer. Connect a synthesizer, drum machine, sampler or MIDI-compatible software instrument to hear its output. The BLE test branch supports configurable USB and Bluetooth MIDI outputs and external clock input over USB, TRS or BLE.

> **Development status — 11 November 2026:** Stable musical work is preserved on [`dev/tangram`](https://github.com/Mr-Roboto/fm1-tangram/tree/dev/tangram). The isolated [`dev/tangram-ble-midi-spike`](https://github.com/Mr-Roboto/fm1-tangram/tree/dev/tangram-ble-midi-spike) branch has successfully built and been installed on an FM-1. Its new four-page GLO interface, BLE connection-status display, and wireless MIDI-note output to Korg iM1 on an iPad have been confirmed on real hardware. BLE external clock reception, independent MIDI Clock output, long-run stability, and all reboot-persistence cases are **not yet hardware-verified**.

## Musical approach

Tangram is designed for music that **develops instead of merely repeats**:

- Multiple voices play patterns of different lengths and retain their own phases, allowing slowly shifting relationships and polymetric movement.
- ARP and MOTIF generators support different musical traversal directions, timing and groove.
- Seed-based, controlled variation supports coherent, reproducible starting material.
- The Composer structures a performance into sections, with changing activity, tension and timing while respecting live intervention.
- MIDI delay, density and ratcheting offer further rhythmic complexity without requiring additional audio effects inside the FM-1.

The goal is an instrument that can sustain a musical idea, transform it gradually, and still give the performer immediate control.

## Core features

| Area | Current capabilities |
| --- | --- |
| **Voices** | Up to seven instrument roles, with independent MIDI channels, generators, pattern lengths and phases |
| **Generators** | ARP, MOTIF and further inherited generative modes; configurable direction, groove, rate, gate, register and pitch phase where supported |
| **Musical control** | Root, scale, voice density, global density, ratcheting, LOCK, MUTATE, DICE and undo |
| **Composer** | Seven musical sections, selectable starting section, activity ramps, controlled development, tension and phase-preserving return of earlier motifs |
| **Drums** | HEARTBEAT: four General MIDI drum instruments on one MIDI track, with selectable parts and groove |
| **MIDI effects** | Per-voice MIDI delay with time, feedback and decay controls |
| **Performance display** | Individual pattern strips, source-note and timing playheads, suppressed-note indication and Composer counters |
| **Interface** | FM-1 buttons and encoders, contextual on-device help and six display themes |
| **Persistence** | Current project settings and six musical snapshot slots; device-wide settings stored separately |
| **Clock** | Internal clock with separately selectable USB/BLE output; external sync input via USB/TRS/BLE or combinations (experimental branch) |

Tangram uses the existing FM-1 panel, screen and controls. No laptop, DAW or companion application is required to operate the sequencer after installation.

## Voices and built-in templates

Tangram currently defines these **seven roles**: **Pluck 1, Pluck 2, Bass, Pad, Lead, Drone and Heartbeat**. Not every role needs to be active in a performance.

The built-in templates are *starting configurations*, not user performance-preset slots:

| Template | Voices | Instrumentation |
| --- | ---: | --- |
| **String Trio** | 3 | Pluck 1, Pluck 2, Bass |
| **Full Ensemble** | 6 | Pluck 1, Pluck 2, Bass, Pad, Lead, Heartbeat |
| **Ambient Drift** | 4 | Drone, Pluck 1, Pluck 2, Pad |

**String Trio** is the default startup template. The internal architecture supports up to seven roles even though the current templates use fewer at once.

## Tangram Composer

The Composer is an optional higher-level musical controller built on the existing sequencer and arranger. It is intended for extended performances rather than a fixed, linear song file.

Its form follows:

**INTRO → A → DEVELOPMENT → B → BREAKDOWN → RETURN → OUTRO → INTRO …**

Depending on the selected form, phrase length, evolution level and tension, the Composer can:

- Bring voices in and out through staggered activity ramps.
- Develop eligible melodic patterns through controlled loop-length, rate and register changes.
- Build harmonic tension in B and release it in BREAKDOWN.
- Restore captured thematic material in RETURN while preserving the relevant musical phases.
- Respect LOCK and manual edits instead of overwriting the performer's decisions.

A starting section can be selected from **HOME COMPOSE** before PLAY. The Composer currently does **not** save that starting-section choice as part of the persistent project record; including it in future Performance Presets is planned.

**TIME**, **PART** and **TOTAL** counters show elapsed playback time, bars in the current section, and total bars. Their STOP/PLAY behavior was checked successfully on FM-1 hardware on 5 November 2026.

Further details: [Composer](docs/composer.md) · [Composer Phase II](docs/composer-phase-ii.md).

## HEARTBEAT drum voice

HEARTBEAT is a single MIDI drum voice that produces four General MIDI instruments without harmonic transposition:

| Instrument | MIDI note |
| --- | ---: |
| Bass drum | 36 |
| Snare drum | 38 |
| Closed hi-hat | 42 |
| Open hi-hat | 46 |

The **PARTS** control enables progressively larger subsets of the kit; **GROOVE** offers **SPARSE**, **STEADY** and **DRIVE**. The basic kick and snare pulse is retained while the hi-hat activity varies. Full Ensemble normally assigns HEARTBEAT to MIDI channel 10, but channel assignments are configurable.

See [Heartbeat drums](docs/heartbeat.md).

## Playing and editing on the FM-1

The interface is organized into the FM-1's existing sections. Some sections have multiple subpages:

| Section | Purpose |
| --- | --- |
| **HOME** | QUICK voice controls, PLAY direction/delay controls, COMPOSE configuration and counters |
| **ENV** | TIMING (rate, gate, loop, phase) and ROUTING (channel, register, voice density, tension) |
| **ARP** | Generator, harmony and direction controls |
| **LFO** | CC lane and scene information |
| **SEQ** | Musical motion, groove and loop-related controls |
| **FX** | Per-voice MIDI delay settings |
| **GLO** | Global SETUP, MIDI routing, APPEARANCE and PERFORMANCE (experimental BLE branch) |

The **MASTER** control adjusts global musical density rather than audio volume. Each voice also has its own density setting; when Composer is active, section activity contributes another factor. Musical transport and phases continue even when new notes are suppressed.

The display distinguishes the **red source-note playhead** (the note position actually played) from the **grey time playhead** (the advancing pattern position). These markers remain independent of MIDI delay echoes.

Six display themes are available: **CLASSIC, GRAPHITE, AMBER, NOCTURNE, FIRE and MONO**. Themes and clock settings are device-global, not part of the musical project.

For detailed encoder assignments and contextual behavior, see [FM-1 pages and controls](docs/ui-pages.md) and [Themes and help](docs/ui-themes-and-help.md).

## Saving: projects, snapshots and performance presets

Tangram currently distinguishes between:

1. **The current project:** SAVE stores its musical configuration, including voice parameters, MIDI routing, delay settings, density, arranger/Composer configuration and seed. If SAVE is requested during playback, the flash write waits for a safe stop.
2. **Six snapshot slots:** These store and recall musical engine states. They are separate from the bank of named Performance Presets.
3. **Global device settings:** MIDI/Clock routing, synchronization, lights and display theme are persisted independently of the musical project.

### Named Performance Presets

A dedicated bank of **10 named Performance Presets** stores complete *starting configurations* — including the selected Composer starting section — independently of the three built-in templates and the six snapshot slots. The bank is implemented on the Tangram development branches; presets are managed on **GLO PERFORMANCE**.

Resumable Composer checkpoints and recording every live control movement are **not** part of the current plan.

## Build and installation

The repository uses [fm1-base](https://github.com/charlesvestal/fm1-base) as a Git submodule, with the JieLi toolchain and AC79 SDK supplied separately. Development has been carried out under **WSL2 / Ubuntu**; the build requires Python 3 with Pillow and the dependencies described by fm1-base.

```sh
git clone --recursive https://github.com/Mr-Roboto/fm1-tangram.git
cd fm1-tangram
git switch dev/tangram
./build.sh
```

The development firmware package is generated as:

```text
app/build/tangram.fwsc
```

The project also contains a Python USB-MIDI firmware installation adapter at [`tools/install_firmware.py`](tools/install_firmware.py). For the Windows/WSL workflow, it is run with native **Windows Python** and the required MIDI dependencies; it uses the existing fm1-base update protocol. A project-specific web installation page can also be generated from the site sources.

**Firmware flashing is at your own risk.** Check the target hardware, installed firmware and update procedure before installing. Never assume the MIDI port name alone proves device identity.

Build and simulator instructions are in [`BUILDING.md`](BUILDING.md). The focused device-settings regression (`sh tests/host/run_device_settings.sh`) passed on WSL2 on 11 November 2026. The simulator compiles via `./build.sh --host`. A focused two-scenario USB/BLE smoke gate is available via `sh tests/run_tangram_smoke.sh`; its execution has not yet been confirmed. The old all-in-one `tests/run_tests.sh` still includes legacy project-format checks and must not be treated as an up-to-date green Tangram regression suite.

## Development status and roadmap

**Implemented and hardware-used:** multivoice sequencing, ARP/MOTIF, Composer, HEARTBEAT drums, MIDI delay, per-voice/global density, the dual pattern playheads, six display themes, project saving and Python firmware installation.

**Current:** ten named Performance Presets and experimental BLE MIDI. Verified on FM-1 hardware on 11 November 2026: Bluetooth connection to an iPad, connected/disconnected UI status, four Global pages, visible routing settings, and wireless note playback of Korg iM1. The standalone global-settings host regression passed. Still to test on hardware: BLE clock input, MIDI clock output and destination combinations under load, persistence across power cycles, and longer sessions.

**Deferred:** further Composer evolution and additional instrument-role templates. Live Composer FORM replacement is implemented on the experimental branch and awaiting host/device validation. There are no plans to add an internal sound engine or audio processing.

This is a **pre-release development branch**, not a declared stable release. The latest source and actual local firmware build should be checked before drawing conclusions from older documentation or historical test notes.

## Project documentation

- [Composer and musical form](docs/composer.md)
- [Composer Phase II](docs/composer-phase-ii.md)
- [HEARTBEAT drum generator](docs/heartbeat.md)
- [FM-1 UI controls](docs/ui-pages.md)
- [MIDI delay](docs/midi-delay.md)
- [ARP](docs/arpeggiator.md) and [MOTIF](docs/motif.md)
- [Scales](docs/scales.md), [Directions](docs/directions.md) and [Grooves](docs/grooves.md)
- [Project record format](docs/project-format.md) and [Global settings](docs/device-settings.md)

Some detailed documents originate from earlier development stages and may contain historical verification notes; the source code and confirmed hardware tests take precedence for the current branch.

## Origins, acknowledgements and licence

Tangram builds on the original [Berlinan firmware for the FM-1](https://github.com/jiripisa/fm1-berlinan) and the Berlinan musical engine by **Jiří Píša**. The original project provides important foundations for its generative sequencer, templates and musical behavior. Tangram extends that foundation with its own Composer developments, generators/controls, MIDI effects and interface work. The original author's attribution and copyright notices remain in the source.

Tangram also depends on [fm1-base](https://github.com/charlesvestal/fm1-base) by **Charles Vestal**, components originating in **Felucca / felucca-bsp** by **Leo Kuroshita (Hügelton Instruments)**, and Berkeley SoftFloat. These dependencies retain their respective licences and notices.

**Tangram is licensed under [GPL-2.0-or-later](LICENSE).** See the repository and its submodules for applicable third-party notices.
