# Berlinan on the FM-1: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An FM-1 firmware on fm1-base that runs a C port of Berlinan's engine, plays the Mac's output exactly (notes and CC) and sends it over USB MIDI, played from the panel.

**Architecture:** A pure C engine (`app/engine/`, prefix `bl_`, no `float`/`double`: IEEE doubles go through a vendored SoftFloat) mirrors `BerlinanEngine` function by function. A Swift reference tool in this repo builds the **unchanged** engine sources from the pinned Berlinan commit and writes reference files; the C engine is tested against them event for event. On top sit the runtime (transport, MIDI timing, clock), the saved project and the panel UI, as an fm1-base app laid out like AMB-1.

**Tech Stack:** C99 (JieLi clang 4.0.1 on the device, the host `cc` for tests), fm1-base `15dff6de7ca0b4229e78845cca13f2a024a799b3` (git submodule `base/`), Berkeley SoftFloat 3e (vendored subset), FreeBSD msun `sin` (transcribed onto SoftFloat), Swift 6.3.3 (reference tool only), POSIX sh.

**Spec:** `docs/superpowers/specs/2026-10-26-berlinan-fm1-design.md`

## Global Constraints

- **Berlinan is read-only.** Nothing is written into `~/Development/Claude/berlin_school_manual` (no edits, no files, no `git worktree`, no builds inside it). Sources are taken with `git -C <repo> archive 0e4a7195cefb9aed5f21ee46bd6eddeebeb776ac`.
- Reference: Berlinan commit `0e4a7195cefb9aed5f21ee46bd6eddeebeb776ac` (v1.2.9), Apple Swift 6.3.3.
- Equivalence: same template + seed + actions at the same ticks + same render windows ⇒ the same `RenderedNote`s and `RenderedCC`s, field for field, in the same order.
- No `float` or `double` anywhere in `app/` (the firmware). fm1-base's build rejects soft-double routines; the engine uses `f64` (Task 3) for every Swift `Double` expression, in Swift's evaluation order (left to right, no fused multiply-add).
- 64-bit multiplies (`uint64_t * uint64_t`, 32×32→64 pieces) only inside `__attribute__((noinline))` helpers (felucca-bsp `docs/CPU.md`; the build prints a note naming any function that breaks this, and that note must not name an engine function).
- Every `sort` that Swift does is a **stable** sort in C (Swift's sort is stable; ties are real, e.g. the pad chord's four notes at position 0).
- Swift `Int` → `int64_t` wherever the value meets ticks, seeds or random ranges; small bounded values (degrees, octaves, counts) may be `int32_t`.
- Firmware: `ble = false`, `usb_audio_in = false`, `usb_audio_out = false`; MIDI out on USB only (`plat_midi_out`); MIDI in from USB and TRS (clock, Start, Stop).
- Templates as the Mac instantiates them: `Project.demo`, `Project.fullEnsemble`, `Project.ambientDrift`, each with key A natural minor (root 9).
- Licence GPL-2.0-or-later (`LICENSE` copied from Berlinan's); SPDX header `GPL-2.0-or-later` on every new file; vendored files keep their own licences.
- Code style: AMB-1's (`~/Development/Claude/fm1-amb/app`): lower-case `snake_case`, `_t` types, a short header comment per file saying what it is, comments as dense as AMB-1's, a unity build (`app/app.c` includes the app's files). Read AMB-1's `app/` and `tests/` before the first line.
- Commits: plain imperative subject lines in AMB-1's style (e.g. "Engine: the seed procedure and the pulsing bass"), no AI attribution or session links. `git status` before every commit. Nothing is pushed.
- Tests: `sh tests/run_tests.sh` runs everything and ends with `all tests passed`.

## Review Focus

1. **A long jam (hours, hundreds of transpositions).** The transposition schedule grows on the Mac without bound; the C schedule must keep answering every query the same within its capacity. → Task 8: `test_sched_compaction`.
2. **Stop, mute, template switch or arranger ending while a drone or pad is sounding.** Expect every sounding note to get its note-off (plus CC 123 per touched channel), nothing left hanging. → Task 13: scenario `stop_flush.bln`.
3. **EXT clock that stops arriving mid-piece** (router unplugged, DAW paused without Stop). Expect playback to hold the last tempo (the Mac freewheels), never jump, and stop only on MIDI Stop. → Task 13: scenario `ext_clock.bln`.
4. **A corrupted, old or foreign saved record** (power lost, another firmware's data). Expect the default template with a fresh seed, never a crash or garbage. → Task 14: `test_store_bad_magic` and scenario `persist.bln`.
5. **Many presses within one bar** (degree keys mashed, B-part twice, slots recalled repeatedly). Expect Mac-identical results up to the queues' capacity and a graceful drop (no overflow, a message) beyond it. → Task 11: `test_pending_overflow`.

---

## File Structure

```
LICENSE  README.md  BUILDING.md  .gitignore  .gitmodules  build.sh
base/                         fm1-base submodule (pinned)
app/
  fm1.toml  app.h  app.c      fm1-base contract and the unity build
  project.c                   the saved project, slots, undo, store objects
  runtime.c                   transport: tick clock, render-ahead queue, MIDI out, note registry, INT/EXT clock
  ui.c                        panel mapping, LEDs, help cards, the seven pages
  host.c                      host-simulator hooks (expect, commands, dumpmidi)
  words.tsv  words.allow      screen-reader words
  engine/                     the pure engine (also built alone for the host tests)
    bl.h                      shared types and caps
    f64.h f64.c               IEEE binary64 on SoftFloat; bl_sin
    softfloat/                vendored SoftFloat 3e subset + SOURCE.md + LICENSE
    rng.h rng.c               SplitMix64, seed streams, Swift's random algorithms
    music.h music.c           scales, keys, degree pitch, quantize, fold
    pattern.h pattern.c       patterns, occurrences, pattern ops
    gen.h gen.c               the seven generators
    legit.h legit.c           lifecycle and mutation
    harmony.h harmony.c       schedule, note pipeline, velocity policy
    arrange.h arrange.c       the arrangement state machine
    cc.h cc.c                 CC lanes and scene morphs
    project.h templates.c     engine-side project and the three templates
    live.h live.c             LiveEngine
tests/
  run_tests.sh
  host/engine_test.c          unit tests (one test function per Task 3–10 test name)
  host/equiv_test.c           runs tests/equiv/*.script, compares with *.ref
  host/test.h                 CHECK macro (AMB-1's)
  equiv/                      scripts and committed reference files
  scenarios/*.bln             host-simulator scenarios
tools/reference/
  fetch.sh                    git archive of the pinned commit into tools/reference/.berlinan/ (gitignored)
  Package.swift               targets over the extracted sources + RefTool
  Sources/RefTool/*.swift     the reference writer
  make_refs.sh                regenerates tests/equiv/*.ref
docs/MANUAL.md
```

---

### Task 1: Repository skeleton that builds and boots

**Files:**
- Create: `LICENSE`, `README.md`, `BUILDING.md`, `.gitignore`, `.gitmodules`, `build.sh`, `app/fm1.toml`, `app/app.h`, `app/app.c`, `app/host.c`, `app/words.tsv`, `app/words.allow`, `tests/run_tests.sh`, `tests/host/test.h`, `tests/scenarios/boot.bln`

**Interfaces:**
- Produces: `app_init`, `app_render` (writes silence), `app_panic`, `app_ui_init`, `app_frame`, `app_input_only`, `app_say` (fm1-base contract); `app.h` defines `APP_NAME "BERLINAN"`, `APP_BLOCK 256u`, `APP_TICK`, `APP_LOOP`, `APP_HOST_EXPECT`, `APP_HOST_CMD`, `APP_HOST_REBOOT`, the store enum `{OBJ_PROJ, OBJ_PANEL, OBJ_SLOT0 … OBJ_SLOT5, OBJ_NOBJ}` with `APP_STORE_MAP` offsets inside `0x97000..0xDFFFF` (two 4 KiB sectors each, OBJ_PANEL at `0xDC000` as AMB-1). `tests/run_tests.sh` with a `run NAME CMD` helper as AMB-1's.

- [ ] **Step 1:** `git submodule add https://github.com/charlesvestal/fm1-base base`, check out `15dff6de7ca0b4229e78845cca13f2a024a799b3`, `git submodule update --init --recursive`.
- [ ] **Step 2:** Copy `LICENSE` from Berlinan's working tree (read only). `.gitignore`: `build/`, `.DS_Store`, `tools/reference/.berlinan/`, `tools/reference/.build/`. `fm1.toml`: `name = "BERLINAN"`, `id_prefix = "FM-1_8"`, `usb_manufacturer = "Berlinan"`, `usb_product = "Berlinan FM-1"`, `ble = false`, `usb_audio_in = false`, `usb_audio_out = false`, `words = "words.tsv"`, `include = ["engine"]`, `o2 = []` for now, `licence = "GPL-2.0-or-later"`.
- [ ] **Step 3:** Write the scenario `tests/scenarios/boot.bln`: `wait 500`, `shot boot.png`, `expect playing 0`. Add the `expect playing` check to `app/host.c` (returns 0 until Task 13).
- [ ] **Step 4:** Run `sh tests/run_tests.sh`. Expected: FAIL (no app sources yet).
- [ ] **Step 5:** Write the minimal app (AMB-1's `app.c` shape; the screen shows "BERLINAN"), `build.sh` (AMB-1's, verbatim apart from names), `README.md` (what it is, two paragraphs), `BUILDING.md` (AMB-1's prerequisites and commands).
- [ ] **Step 6:** Run `sh tests/run_tests.sh` (expect `ok host-build`, `ok scenario-boot`, `all tests passed`) and `./build.sh` (expect `app/build/berlinan.fwsc`, no errors in the build's checks).
- [ ] **Step 7:** Commit: "Skeleton: an fm1-base app that boots, with the test runner".

### Task 2: The reference tool and the equivalence format

**Files:**
- Create: `tools/reference/fetch.sh`, `tools/reference/Package.swift`, `tools/reference/Sources/RefTool/main.swift`, `tools/reference/Sources/RefTool/Script.swift`, `tools/reference/Sources/RefTool/Dump.swift`, `tools/reference/make_refs.sh`, `tests/equiv/README.md`, `tests/equiv/smoke.script`, `tests/equiv/smoke.ref`

**Interfaces:**
- Produces: the **script language** and the **reference line format**, both used by Task 11's C runner. Write both into `tests/equiv/README.md`.
  - Script lines: `template demo|full|ambient` · `seed 0xHEX` · `arranger off|classic|slow|ambient` (project-level, before play: the preset `ArrangementConfig`) · `density K` (project-level, K/100) · `window T` (render window in ticks) · `bars N` (total length) · `at TICK ACTION…`. Actions: `transpose D` · `regenerate V` · `reroll` (regenerate every voice unlocked at that tick, in project order) · `blank V` · `lock V 0|1` · `save SLOT` (capture state into slot 0–31) · `recall SLOT` · `key ROOT MODE` (`setKey(Key(root, .preset(mode)))`, mode by its `ModeID` raw value) · `density K` · `bpart` · `voice V on|off` · `tension` (pad voice, else the first voice) · `scene N` (the template's N-th scene, 8 bars) · `arranger on|off` · `advance` · `reseed 0xHEX` (as `AppServices.reseed` while playing: app project gets the seed, the running engine recalls `LiveEngine(appProject).captureState()`) · `load demo|full|ambient 0xHEX` (as `Transport.loadProject` + `applyTemplate`: a new engine from the next bar line, ticks continue). `V` is a voice id (`bedPluck1`…). Actions at the same tick apply in script order, after the window ending at that tick.
  - Reference lines, one event per line: `N <at> <voiceID> <midi> <vel> <gate> <accent> <ratchet>` · `C <at> <voiceID> <controller> <value>` · `H <tick> <16 hex digits>` (state hash after each window whose end crosses a multiple of 16 bars) · `S <tick>` (pending stop tick, once, when it appears). Per window: notes as `render` returned them, then CC as `renderCC` returned them.
  - State hash: FNV-1a 64 over a canonical text built **only from the engine's public API** (`snapshot()` and `captureState()`; the extracted sources are compiled as they are, never patched), defined in `Dump.swift` and in the README field by field: per voice in project order: id, active, blankActive, pendingRequest (kind and tick, or `-`), generation, pattern length, startOffset, locked, lifecycle remaining or `E`, provenance seed, then each note's position, degree, octave, accidental, velocity, gate, accent, ratchet; then the schedule events, the key (root, offsets), density bits as hex, tensionRemaining, the arrangement status (phase raw value, barsInPhase, barsUntilChange, active voice ids) or `-`, and the upcoming events (tick, kind raw value, detail, voice id).
  - Tool modes: `reftool script FILE` (prints the reference), `reftool rng` (Task 4 vectors), `reftool gen`, `reftool mut`, `reftool harmony`, `reftool arrange`, `reftool cc` (Tasks 6–10 vectors; each prints `#` comment headers naming the case).

- [ ] **Step 1:** `fetch.sh`: `git -C "$BERLINAN" archive 0e4a7195cefb9aed5f21ee46bd6eddeebeb776ac Sources/BerlinanEngine Sources/BerlinanRuntime/EventConversion.swift Sources/BerlinanRuntime/MIDI1Event.swift Sources/BerlinanRuntime/NoteRegistry.swift Sources/Berlinan/TemplateStore.swift | tar -x -C tools/reference/.berlinan` (`BERLINAN` defaults to `~/Development/Claude/berlin_school_manual`). `Package.swift`: target `BerlinanEngine` at `.berlinan/Sources/BerlinanEngine` plus the tool's extension file, target `RuntimePure` over the three runtime files, executable `RefTool`. `TemplateStore.swift` is read for its key only (A minor); the tool applies it the same way.
- [ ] **Step 2:** Write `tests/equiv/smoke.script` (`template demo`, `seed 0xBE1271A9`, `window 960`, `bars 32`, `at 15360 transpose 4`).
- [ ] **Step 3:** Implement the tool; run `swift run -c release --package-path tools/reference RefTool script tests/equiv/smoke.script > tests/equiv/smoke.ref` twice and `cmp` the two outputs (deterministic). Check one sanity property in the tool's self-test mode (`reftool selftest`): for `template full` + `arranger classic`, the concatenated notes of `script` with `window 3840` equal `OfflineRenderer.renderNotes(project:bars:)` for the same project.
- [ ] **Step 4:** `make_refs.sh` regenerates every `tests/equiv/*.ref` from its `.script` and the vector files; run it, `git diff` shows only the new files.
- [ ] **Step 5:** Commit: "Reference: a Swift tool over the pinned Berlinan engine, and the equivalence format".

### Task 3: f64, IEEE doubles without `double`

**Files:**
- Create: `app/engine/softfloat/` (SoftFloat 3e: `softfloat.h`, `softfloat_types.h`, `internals.h`, `primitives.h`, `primitiveTypes.h`, `specialize.h`, `platform.h` and the `.c` files the functions below need, `8086-SSE` specialisation), `app/engine/softfloat/SOURCE.md` (URL `http://www.jhauser.us/arithmetic/SoftFloat-3e.zip`, its SHA-256, the file list), `app/engine/softfloat/LICENSE`, `app/engine/f64.h`, `app/engine/f64.c`
- Test: `tests/host/engine_test.c`

**Interfaces:**
- Produces (`f64.h`): `typedef struct { uint64_t bits; } f64;` · `#define F64(bits)` (a constant from its bit pattern; every Swift literal is written this way with its value in a comment) · `f64 f64_add(f64, f64)`, `f64_sub`, `f64_mul`, `f64_div` (round to nearest even) · `int f64_lt(f64, f64)`, `f64_le`, `f64_eq` · `f64 f64_from_i64(int64_t)`, `f64 f64_from_u64(uint64_t)` · `int64_t f64_to_i64(f64)` (truncation, Swift `Int(x)`) · `f64 f64_round(f64)` (Swift `.rounded()`: to nearest, ties away from zero) · `f64 f64_ceil(f64)` (`.rounded(.up)`) · `f64 f64_frac1(f64)` (`truncatingRemainder(dividingBy: 1)`: `x − trunc(x)`) · `f64 f64_max(f64 x, f64 y)` (Swift's `max(x, y)`: `y >= x ? y : x`), `f64 f64_min(f64 x, f64 y)` (Swift's `min(x, y)`: `y < x ? y : x`) · `f64 bl_sin(f64)`.
- `bl_sin`: FreeBSD msun `s_sin.c`, `k_sin.c`, `k_cos.c`, `e_rem_pio2.c` (tag `release/14.1.0`), transcribed onto `f64_*` in the C source's operation order; only the paths for |x| < 2π + 1 are needed (the argument is `2π·x`, x in [0, 1)); larger arguments are out of range (assert).

- [ ] **Step 1: Write the failing tests** (`engine_test.c`, host-only code may use `double`):
  - `test_f64_ops`: 2,000,000 pairs from a fixed SplitMix64 stream (raw bit patterns, plus a set of edge values: ±0, subnormals, 1, 0.5, 2^53, 2^53+1 as int, 0.1, 0.3, 0.85, 1e-300, 1e300) — `add/sub/mul/div/lt/le/eq` bit-identical to the host's `double`; `f64_from_i64`, `f64_to_i64`, `f64_round` (vs `round`), `f64_ceil` (vs `ceil`), `f64_frac1` (vs `fmod(x, 1)`) for finite values in range.
  - `test_sin_cc_level`: for the templates' two lanes (cycle 16 bars, center 0.6, depth 0.3; cycle 32 bars, center 0.5, depth 0.4; phase 0) and every grid tick t = 0, 40, 80 … over 10,000 bars, compute x as `ModulationEngine` does (`frac(Double(t) / (cycleBars · 3840) + phase)`, host doubles) and the CC value `round(127 · clamp(center + depth / 2 · s))` once with `s = bl_sin(2π · x)` and once with the host's `sin`: 0 mismatches. Also print (do not assert) how many raw `bl_sin` results differ from `sin` in the last bit.
- [ ] **Step 2:** Run `sh tests/run_tests.sh`. Expected: FAIL (`f64.h` missing).
- [ ] **Step 3:** Vendor SoftFloat (only the files the listed functions need), write `f64.c`, transcribe msun's sine. Add an `engine` run to `tests/run_tests.sh`: `cc -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -Iapp/engine -Iapp/engine/softfloat tests/host/engine_test.c app/engine/*.c app/engine/softfloat/*.c -lm`. Add a `no-double` run: `! grep -nwE 'double|float' app/*.c app/*.h app/engine/*.[ch]` (comments included; write "IEEE binary64" in prose instead).
- [ ] **Step 4:** Run tests. Expected: `ok engine`, `ok no-double`. If `test_sin_cc_level` reports any mismatch, stop and report the cases instead of tuning the sine.
- [ ] **Step 5:** Commit: "Engine: IEEE doubles on SoftFloat, and msun's sine".

### Task 4: Randomness, bit for bit with Swift 6.3.3

**Files:**
- Create: `app/engine/bl.h`, `app/engine/rng.h`, `app/engine/rng.c`, `tests/equiv/rng.ref`
- Test: `tests/host/engine_test.c`

**Interfaces:**
- Produces (`bl.h`): `typedef int64_t bl_tick_t;` · `#define BL_PPQN 960` · `#define BL_TICKS_PER_BAR 3840` (4/4 is the only meter the templates use; the engine keeps the meter as `bl_meter_t {int32_t beats, unit;}` with `bl_ticks_per_beat`/`bl_ticks_per_bar` so the code reads as Swift's) · caps `BL_MAX_VOICES 7`, `BL_MAX_NOTES 64`, `BL_MAX_EVENTS 256`, `BL_MAX_LANES 2`, `BL_MAX_SCENES 5`.
- Produces (`rng.h`): `typedef struct { uint64_t state; } bl_rng_t;` · `void bl_rng_seed(bl_rng_t *r, uint64_t seed)` · `uint64_t bl_rng_next(bl_rng_t *r)` (SplitMix64 as `Rand.swift`) · `uint64_t bl_seed_derive(uint64_t master, ...)` (NULL-terminated `const char *` labels; FNV-1a 64 over each label's bytes then `0x1F`, then one SplitMix64 round of `master ^ hash`) · `void bl_dec(int64_t v, char buf[24])` (Swift `String(Int)`) · `uint64_t bl_rand_below(bl_rng_t *r, uint64_t bound)` · `int64_t bl_rand_in(bl_rng_t *r, int64_t lo, int64_t hi)` (`Int.random(in: lo...hi)`) · `int64_t bl_rand_upto(bl_rng_t *r, int64_t lo, int64_t hi)` (`lo..<hi`) · `int bl_rand_bool(bl_rng_t *r)` · `f64 bl_rand_unit(bl_rng_t *r)` (`Double.random(in: 0..<1)`) · `int64_t bl_rand_index(bl_rng_t *r, int64_t count)` (`randomElement`: count > 0; the caller treats an empty collection as "no draw").
- The algorithms, measured on the installed Swift 6.3.3 (2026-10-26):
  - `next(upperBound: b)`: Lemire. `m = x·b` (128-bit); if `low(m) < b`: `t = (0 − b) % b`; while `low(m) < t`: draw again. Result `high(m)`.
  - `Int.random(in: lo...hi)` = `lo + next(upperBound: hi − lo + 1)` (one draw unless rejected); `lo..<hi` = `lo + next(upperBound: hi − lo)`.
  - `Bool.random` = `((next() >> 17) & 1) == 0`.
  - `Double.random(in: 0..<1)` = `(next() & (2^53 − 1)) · 2^−53` — the **low** 53 bits (one draw).
  - `randomElement` = element at `Int.random(in: 0..<count)`.

- [ ] **Step 1:** Extend the reference tool: `reftool rng` prints, for seeds 0, 1, 0xBE1271A9, 2477861893 and 20 more from a fixed list, the first 64 `next()` values, `SeedStream.derive` for 16 label lists (including `["voice","bass","mut","12"]`, `["humanize","-240"]`, `["arrangement"]`), 2,000 `Int.random(in:)` draws over ranges {1...12, 0...3, -7...7, 0..<16, 2...4, 102...138, 0..<1}, 2,000 `Bool.random`, 2,000 `Double.random(in: 0..<1)` bit patterns, 500 `randomElement` indices for counts 1–17. Write `tests/equiv/rng.ref`.
- [ ] **Step 2: Write the failing test** `test_rng_vectors`: parses `tests/equiv/rng.ref` and replays each line through `rng.c`; every value equal.
- [ ] **Step 3:** Run tests. Expected: FAIL.
- [ ] **Step 4:** Implement `rng.c`; the 64×64→128 multiply in its own `noinline` function built from four 32×32→64 products.
- [ ] **Step 5:** Run tests. Expected: `ok engine`.
- [ ] **Step 6:** Commit: "Engine: SplitMix64, seed streams and Swift's random algorithms".

### Task 5: Scales, keys and degree pitch

**Files:**
- Create: `app/engine/music.h`, `app/engine/music.c`
- Test: `tests/host/engine_test.c`

**Interfaces:**
- Produces: `enum { BL_MODE_NATURAL_MINOR, BL_MODE_DORIAN, BL_MODE_PHRYGIAN, BL_MODE_MIXOLYDIAN, BL_MODE_LYDIAN, BL_MODE_LYDIAN_DOMINANT, BL_MODE_HARMONIC_MINOR, BL_MODE_MINOR_PENTATONIC, BL_MODE_LOCRIAN, BL_MODE_MAJOR, BL_NMODES };` (the `ModeID` order) · `extern const char *const BL_MODE_RAW[BL_NMODES]` (`"naturalMinor"`…) · `typedef struct { uint8_t mode, count; int8_t off[12]; } bl_scale_t;` · `typedef struct { int32_t root; bl_scale_t scale; } bl_key_t;` · `void bl_scale_preset(int mode, bl_scale_t *out)` · `typedef struct { int32_t degree, octave, accidental; } bl_pitch_t;` · `int64_t bl_floor_div(int64_t, int64_t)`, `int64_t bl_floor_mod(int64_t, int64_t)` · `int32_t bl_resolve(const bl_key_t *, bl_pitch_t, int32_t ref_c)` · `bl_pitch_t bl_quantize(const bl_key_t *, int32_t midi, int32_t ref_c)` (ties go down, candidate order as Swift's loops) · `uint8_t bl_fold(uint8_t low, uint8_t high, int32_t note)`.

- [ ] **Step 1: Write the failing tests** `test_music`: preset offsets for all ten modes as in `Tonality.swift`; `bl_resolve` in A minor (root 9) of degree 0/7/−1/9 with octave ±1 at refC 60 → 69, 81, 67, 84 (and at refC 36 → 45, 57, 43, 60); `bl_quantize` in A minor of midi 60…72 at refC 60 (tie down: midi 70 → degree of 69; write the expected 13 degree/octave pairs from the Swift implementation by hand); `bl_fold(24, 48, 60) == 48`, `bl_fold(55, 79, 40) == 52`; `bl_floor_mod(-1, 7) == 6`, `bl_floor_div(-1, 7) == -1`.
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run. Expected: `ok engine`.
- [ ] **Step 5:** Commit: "Engine: scales, keys and degree pitch".

### Task 6: Patterns and the seven generators

**Files:**
- Create: `app/engine/pattern.h`, `app/engine/pattern.c`, `app/engine/gen.h`, `app/engine/gen.c`, `tests/equiv/gen.ref`
- Test: `tests/host/engine_test.c`

**Interfaces:**
- Consumes: Tasks 3–5.
- Produces (`pattern.h`): `typedef struct { bl_tick_t position, gate; bl_pitch_t pitch; uint8_t velocity, accent, ratchet; } bl_pnote_t;` · `typedef struct { bl_tick_t length, start_offset; uint8_t n, generator, locked; int16_t remaining; /* −1 = established */ uint64_t seed; bl_pnote_t note[BL_MAX_NOTES]; } bl_pattern_t;` · `typedef struct { bl_tick_t at; uint8_t index; } bl_occ_t;` · `int bl_occurrences(const bl_pattern_t *, bl_tick_t lo, bl_tick_t hi, bl_occ_t *out, int max)` (sorted by `at`, then note position, stable) · `int bl_has_onset(const bl_pattern_t *, bl_tick_t global)` · `void bl_with_length(bl_pattern_t *, bl_tick_t new_len)` · `void bl_density_cap(const bl_pattern_t *in, f64 density, bl_pattern_t *out)` · `void bl_thin_to_roots(bl_pattern_t *)` · `void bl_move_accents(bl_pattern_t *, uint64_t seed)`.
- Produces (`gen.h`): `enum { BL_ROLE_BED1, BL_ROLE_BED2, BL_ROLE_BASS, BL_ROLE_PAD, BL_ROLE_LEAD, BL_ROLE_DRONE, BL_ROLE_RHYTHM, BL_ROLE_AUX };` (`VoiceRole` order) · `enum { BL_GEN_SEED_PROCEDURE, BL_GEN_PULSING_BASS, BL_GEN_STAGED_PLUCK, BL_GEN_BASS_MELODY, BL_GEN_CHORD_LAYER, BL_GEN_HEARTBEAT, BL_GEN_DRONE };` · `typedef struct { uint8_t kind; int16_t a, b; } bl_gencfg_t;` (seedProcedure: a = bars, b = mutations; pulsingBass: a = accents; stagedPluck, bassMelody, drone: a = bars; chordLayer: a = periodBars) · `void bl_make_pattern(const bl_gencfg_t *, int role, const bl_key_t *, uint64_t seed, bl_pattern_t *out)` (`PatternFactory.make`; bassMelody here uses the pedal) · `void bl_make_bass_melody(int bars, const struct bl_sched *, uint64_t seed, bl_pattern_t *out)` (Task 8 defines `bl_sched_t`; declare it incomplete here).
- `highMidLow`, `drunkardsWalk`, `arpeggiator` and decoupled gate cycles are not ported (no template and no control reaches them).

- [ ] **Step 1:** `reftool gen`: for each generator config the templates use (`seedProcedure(1,4)` and `(1,3)` as bed1/bed2/bass role, `pulsingBass(3)`, `stagedPluck(2)`, `bassMelody(16)` with the pedal and with the demo schedule, `chordLayer(7)`, `heartbeat`, `drone(8)`), in A minor and C minor, 200 seeds each: print the pattern (length, every note's fields). Also `densityCap` at densities 0, 0.01, 0.35, 0.5, 0.99 of 50 of those patterns, `thinToRoots` and `moveAccents` (seed) of 50. Write `tests/equiv/gen.ref`.
- [ ] **Step 2: Write the failing test** `test_gen_vectors` replaying `gen.ref` (the demo schedule for the bass-melody cases is filled into a `bl_sched_t` directly, already sorted; Task 8 brings the merge); plus `test_gen_max_notes`: the largest `n` over all cases ≤ `BL_MAX_NOTES` (bassMelody(16) peaks at 64).
- [ ] **Step 3:** Run. Expected: FAIL.
- [ ] **Step 4:** Implement, generator by generator, mirroring every draw in Swift's order (a `Set` that is iterated is iterated sorted, as Swift's code sorts it; a `Set` used for membership is a plain array scan).
- [ ] **Step 5:** Run. Expected: `ok engine`.
- [ ] **Step 6:** Commit: "Engine: patterns and the generators the templates use".

### Task 7: Legitimization and mutation

**Files:**
- Create: `app/engine/legit.h`, `app/engine/legit.c`, `tests/equiv/mut.ref`
- Test: `tests/host/engine_test.c`

**Interfaces:**
- Consumes: Task 6.
- Produces: `typedef struct { const char *id; uint8_t role, reg_low, reg_high, participates, anchor; int32_t ref_c; bl_pattern_t pat; } bl_voice_t;` (`anchor`: 0 none, 1 beat1, 2 beats1and3) · `void bl_advance_boundary(bl_pattern_t *)` · `int bl_is_mutable(const bl_pattern_t *)` · `void bl_mutate(bl_voice_t *, const bl_key_t *, uint64_t seed, int repeats, f64 ratchet_p)` (slides off: the templates never enable them; ratchet probability `0.15` = `F64(0x3FC3333333333333)` by default).

- [ ] **Step 1:** `reftool mut`: for 300 (pattern, seed) pairs taken from the `gen` cases, roles bed1/bass/pad/rhythm/drone, `repeats` 4 and 16: print the voice before and after `Legitimizer.mutate`, plus 20 lifecycle sequences of `advanceAtLoopBoundary`. Write `tests/equiv/mut.ref`.
- [ ] **Step 2: Write the failing test** `test_mut_vectors`.
- [ ] **Step 3:** Run. Expected: FAIL.
- [ ] **Step 4:** Implement.
- [ ] **Step 5:** Run. Expected: `ok engine`.
- [ ] **Step 6:** Commit: "Engine: legitimization and mutation".

### Task 8: The transposition schedule and the note pipeline

**Files:**
- Create: `app/engine/harmony.h`, `app/engine/harmony.c`, `tests/equiv/harmony.ref`
- Test: `tests/host/engine_test.c`

**Interfaces:**
- Consumes: Tasks 5–7.
- Produces: `typedef struct { int32_t bar, offset; } bl_tev_t;` · `typedef struct bl_sched { uint16_t n; bl_tev_t ev[BL_MAX_EVENTS]; } bl_sched_t;` · `void bl_sched_merge(bl_sched_t *, const bl_tev_t *add, int n, int32_t now_bar)` (`TranspositionSchedule(events: old + add)`: last wins per bar, sorted by bar) · `int32_t bl_sched_offset(const bl_sched_t *, int32_t bar)` · `int32_t bl_quantized_bar(bl_tick_t request)` (next bar, a request on a bar binds there) · `typedef struct { bl_tick_t at, gate; uint8_t voice, midi, velocity, accent, ratchet; } bl_note_t;` · `int bl_render_voice(const bl_voice_t *, uint8_t voice_index, const bl_sched_t *, const bl_key_t *, bl_tick_t lo, bl_tick_t hi, bl_note_t *out, int max)` (`HarmonyEngine.render` for one voice) · `uint8_t bl_velocity(uint8_t vel, int accent, uint64_t pattern_seed, bl_tick_t local)` (the default `VelocityPolicy`: boost 20, range ±10).
- **Compaction** (when a merge would exceed `BL_MAX_EVENTS`): drop events with `16 ≤ bar` that are followed by another event with `bar ≤ now_bar` — queries for bars < 16 (the bass melody's 16-bar walk reads them) and for bars ≥ `now_bar` keep their answers. If still full, the merge drops the new events and returns −1 (the UI says "SCHEDULE FULL").

- [ ] **Step 1:** `reftool harmony`: schedules built from 100 random event lists (dup bars included) with their `offset(atBar:)` for bars 0–200; `quantizedEventBar` for 50 ticks; `HarmonyEngine.render` + velocity for the template voices over 64 bars at 3 window sizes. Write `tests/equiv/harmony.ref`.
- [ ] **Step 2: Write the failing tests** `test_harmony_vectors` and `test_sched_compaction`: 2,000 merges of one event each at increasing bars, `now_bar` following; after each, `bl_sched_offset` for bars 0–15 and `now_bar … now_bar + 64` equals a reference computed with an unbounded array in the test.
- [ ] **Step 3:** Run. Expected: FAIL.
- [ ] **Step 4:** Implement.
- [ ] **Step 5:** Run. Expected: `ok engine`.
- [ ] **Step 6:** Commit: "Engine: the transposition schedule and the note pipeline".

### Task 9: The arrangement state machine

**Files:**
- Create: `app/engine/arrange.h`, `app/engine/arrange.c`, `tests/equiv/arrange.ref`
- Test: `tests/host/engine_test.c`

**Interfaces:**
- Consumes: Tasks 3, 4. Voices are indices in project order, given by their roles.
- Produces: `enum { BL_PH_BUILD, BL_PH_BED, BL_PH_VARIATION, BL_PH_BPART, BL_PH_PAYOFF, BL_PH_SUBTRACT, BL_PH_ENDING };` · `extern const char *const BL_PHASE_RAW[7]` (`"build"`…) · `typedef struct { uint8_t enabled, ambient, reprise; int16_t min_bars, max_bars; } bl_arrcfg_t;` (+ `bl_arr_classic`, `bl_arr_slow`, `bl_arr_ambient` presets) · `enum { BL_DIR_ACTIVATE, BL_DIR_DEACTIVATE, BL_DIR_NUDGE, BL_DIR_ACCENT, BL_DIR_BLANK, BL_DIR_DENSITY, BL_DIR_BPART, BL_DIR_THIN, BL_DIR_STOP };` · `typedef struct { uint8_t kind, voice; f64 value; } bl_dir_t;` · `typedef struct { … } bl_arr_t;` (phase, barsInPhase, activeVoices as an ordered index list, buildOrder, ambientPeriodBars, density f64, rng, barsUntilChange, eventsInPhase, roundRobin, stopRequested) · `void bl_arr_init(bl_arr_t *, const bl_arrcfg_t *, const uint8_t *roles, int nvoices, uint64_t seed, int adopting, uint32_t active_mask, int ambient_period_bars)` · `int bl_arr_tick_bar(bl_arr_t *, uint32_t locked_mask, bl_dir_t *out, int max)` · `int bl_arr_advance(bl_arr_t *, uint32_t locked_mask, bl_dir_t *out, int max)` · `int bl_build_order(const uint8_t *roles, int n, uint8_t *order)`.

- [ ] **Step 1:** `reftool arrange`: for the three presets × the three templates' voice lists × 30 seeds × locked sets {none, bass, bedPluck1+pad}, the directives of 400 `tickBar` calls with `advance()` at 10 fixed bars, and the SM fields after each. Write `tests/equiv/arrange.ref`.
- [ ] **Step 2: Write the failing test** `test_arrange_vectors`.
- [ ] **Step 3:** Run. Expected: FAIL.
- [ ] **Step 4:** Implement.
- [ ] **Step 5:** Run. Expected: `ok engine`.
- [ ] **Step 6:** Commit: "Engine: the arrangement state machine".

### Task 10: CC lanes and scene morphs

**Files:**
- Create: `app/engine/cc.h`, `app/engine/cc.c`, `tests/equiv/cc.ref`
- Test: `tests/host/engine_test.c`

**Interfaces:**
- Consumes: Tasks 3, 4.
- Produces: `typedef struct { uint8_t shape; /* sine only in v1 */ uint8_t target; /* index into the voice's cc map */ f64 cycle_bars, depth, center, phase; } bl_cclane_t;` · `typedef struct { bl_tick_t at; uint8_t voice, controller, value; } bl_cc_t;` · `int bl_cc_lane(const bl_cclane_t *, uint8_t controller, uint8_t voice, bl_tick_t lo, bl_tick_t hi, bl_cc_t *out, int max)` (grid 40 ticks, emit on change, tick 0 always) · `typedef struct { f64 start, end; bl_tick_t t0, t1; } bl_morph_t;` · `f64 bl_morph_value(const bl_morph_t *, bl_tick_t)` · `int bl_morph_emit(const bl_morph_t *, uint8_t voice, uint8_t controller, bl_tick_t lo, bl_tick_t hi, bl_cc_t *out, int max)` (the `sceneEmissions` loop for one morph).

- [ ] **Step 1:** `reftool cc`: the templates' lanes (16-bar 0.3/0.6, 32-bar 0.4/0.5) over 0–10,000 bars in windows of 37, 960 and 3840 ticks; 40 morphs (start/end from the ambient scenes and 0.5, 8 bars) over windows crossing their start and end. Write `tests/equiv/cc.ref`.
- [ ] **Step 2: Write the failing test** `test_cc_vectors`.
- [ ] **Step 3:** Run. Expected: FAIL.
- [ ] **Step 4:** Implement.
- [ ] **Step 5:** Run. Expected: `ok engine`.
- [ ] **Step 6:** Commit: "Engine: CC lanes and scene morphs".

### Task 11: The live engine and the equivalence runner

**Files:**
- Create: `app/engine/project.h`, `app/engine/templates.c`, `app/engine/live.h`, `app/engine/live.c`, `tests/host/equiv_test.c`, `tests/equiv/live_*.script` + `.ref`
- Modify: `tests/run_tests.sh` (an `equiv` run), `app/fm1.toml` (`o2 = ["engine/live.c", …]` all engine units, like AMB-1's `dsp/amb.c`)

**Interfaces:**
- Consumes: Tasks 3–10.
- Produces (`project.h`): `typedef struct { const char *id; uint8_t role, reg_low, reg_high, channel, anchor, nlanes; int32_t ref_c; int8_t phase_steps; bl_gencfg_t gen; bl_cclane_t lane[BL_MAX_LANES]; uint8_t ncc; const char *cc_name[2]; uint8_t cc_num[2]; } bl_vcfg_t;` · `typedef struct { const char *name; uint8_t n; uint8_t voice[BL_MAX_VOICES]; uint8_t target[BL_MAX_VOICES]; f64 value[BL_MAX_VOICES]; } bl_scene_t;` · `typedef struct { uint64_t seed; int32_t bpm; bl_key_t key; uint8_t nvoices, has_arr, nscenes; bl_vcfg_t voice[BL_MAX_VOICES]; bl_sched_t transposition; bl_arrcfg_t arr; f64 density; int8_t scene_by_phase[7]; bl_scene_t scene[BL_MAX_SCENES]; } bl_project_t;` · `enum { BL_TPL_DEMO, BL_TPL_FULL, BL_TPL_AMBIENT, BL_NTPL };` · `void bl_template(int which, bl_project_t *out)` (A minor, the template's own seed).
- Produces (`live.h`): `typedef struct { bl_voice_t v[BL_MAX_VOICES]; int64_t generation[BL_MAX_VOICES]; bl_sched_t sched; } bl_state_t;` (`StateSnapshot`) · `enum { BL_A_TRANSPOSE, BL_A_REGENERATE, BL_A_BLANK, BL_A_LOCK, BL_A_RECALL, BL_A_KEY, BL_A_DENSITY, BL_A_BPART, BL_A_VOICE_ACTIVE, BL_A_TENSION, BL_A_SCENE, BL_A_ARRANGER, BL_A_ADVANCE };` · `typedef struct { uint8_t kind, voice; int32_t arg; f64 value; bl_key_t key; const bl_state_t *state; /* RECALL: copied at enqueue */ } bl_action_t;` · `typedef struct bl_live bl_live_t;` (the full `LiveEngine` state; `sizeof` reported by a test, placed in the pool by the app) · `void bl_live_init(bl_live_t *, const bl_project_t *)` · `int bl_live_enqueue(bl_live_t *, const bl_action_t *, bl_tick_t request)` (0 ok, −1 dropped: queue full) · `int bl_live_render(bl_live_t *, bl_tick_t lo, bl_tick_t hi, bl_note_t *out, int max)` · `int bl_live_render_cc(const bl_live_t *, bl_tick_t lo, bl_tick_t hi, bl_cc_t *out, int max)` · `void bl_live_capture(const bl_live_t *, bl_state_t *out)` · `bl_tick_t bl_live_stop_tick(const bl_live_t *)` (−1 none) · `void bl_live_status(const bl_live_t *, bl_status_t *out)` (the UI mirror: per voice active, blank, pending request kind/tick, remaining, length, locked, generation; upcoming events (kind, tick, detail, voice) sorted by tick; arranger present, phase, barsInPhase, barsUntilChange, active list; density; tension remaining; schedule offset at a bar) · `uint64_t bl_live_hash(const bl_live_t *)` (Task 2's canonical text, FNV-1a 64).
- Queue caps (Swift's are unbounded): pending global changes 32, pending recalls 2 (each holds a `bl_state_t` copy), arranger enables 8, advances 8.
- `equiv_test.c` reads a `.script`, plays it with the same windows and app-level rules as the reference tool (including `reroll`, `tension`, `reseed`, `load`), prints the same lines, and compares with the `.ref` line by line; on the first difference it prints the line number, both lines, and the last `H` line that matched.

- [ ] **Step 1:** Write the scripts (each run with `window` 240, 960 and 37 as three files, so chunk safety is covered): `live_plain_{demo,full,ambient}` (seed of the template, 512 bars, no actions) · `live_seeds` (each template × 8 seeds × 256 bars) · `live_actions_{demo,full,ambient}` (every action of Task 2 at least three times, spread over 400 bars, including `save`/`recall`, `reseed`, `load` to another template, `key` to dorian and back, `density 35` then `100`, `bpart` twice in one bar, `tension` three times) · `live_arranger_{classic,slow,ambient}` (full ensemble, `arranger` preset, 800 bars: reaches the ending and its stop) · `live_arranger_toggle` (`arranger on` at bar 20, `advance` ×3, `arranger off` at bar 200). Run `tools/reference/make_refs.sh`.
- [ ] **Step 2: Write the failing tests:** `equiv_test` over every `tests/equiv/live_*.script`; `test_pending_overflow` (engine_test): 40 `bl_live_enqueue` density changes in one bar → the first 32 return 0, the rest −1, and rendering on gives the same output as enqueueing only the first 32 (checked against a second engine).
- [ ] **Step 3:** Run. Expected: FAIL.
- [ ] **Step 4:** Implement `templates.c` and `live.c` (`LiveEngine.swift` and the parts of `OfflineRenderer.swift` it calls: `materialize`, `makePattern`, the ambient legitimization scaling, `processBoundary` steps 1, 2, 2c, 2d, 3, 4, `applyBPart`, `applyRecall`, the arranger boundary with scenes and reprise, the tension one-shots, `renderCC` with scene emissions). Melody, promotion, alternating, MIDI delay, replace-pattern, accumulator and gate ramp are left out; their fields are absent from the state and the hash text writes them as Swift's defaults.
- [ ] **Step 5:** Run. Expected: `ok equiv` for every script.
- [ ] **Step 6:** Commit: "Engine: the live engine, equal to Berlinan's on every script".

### Task 12: The engine on the device

**Files:**
- Modify: `app/app.c` (include the engine units in the unity build or as `o2` units), `app/fm1.toml`
- Create: `tests/scenarios/engine_load.bln`

**Interfaces:**
- Consumes: Task 11.
- Produces: the engine running inside the app on the host simulator and the device build, nothing new for later tasks.

- [ ] **Step 1:** Scenario `engine_load.bln`: an `app_host_cmd` `selfrender TEMPLATE BARS` that renders the template for BARS in windows of 240 inside the app and prints the count and a hash of the notes; `expect` the hash that `equiv_test` printed for the same script (`live_plain_full` window 240, 128 bars).
- [ ] **Step 2:** Run. Expected: FAIL (command missing).
- [ ] **Step 3:** Implement the command; place `bl_live_t` and the render buffers with `PLAT_POOL`.
- [ ] **Step 4:** Run tests, then `./build.sh`. Expected: tests pass; the build's checks print no error, no 64-bit multiply note naming an engine or softfloat function, "no soft-double routines", and RAM/pool sizes within limits (paste the size line into the commit message body).
- [ ] **Step 5:** Commit: "The engine in the firmware: sizes and the CPU's multiply rule checked".

### Task 13: The runtime: transport, MIDI out and clock

**Files:**
- Create: `app/runtime.c`, `tests/scenarios/play_midi.bln`, `tests/scenarios/stop_flush.bln`, `tests/scenarios/ext_clock.bln`, `tests/scenarios/tempo.bln`
- Modify: `app/app.c`, `app/host.c`, `tools/reference/Sources/RefTool/main.swift` (a `midi` mode), `tests/equiv/` (`midi_full.script` + `.ref`)

**Interfaces:**
- Consumes: Task 11.
- Produces: `void rt_init(void)` · `void rt_play(void)` / `void rt_stop(void)` · `int rt_playing(void)` · `bl_tick_t rt_tick(void)` (the sounding tick) · `bl_tick_t rt_head(void)` (the render head, where actions bind) · `void rt_action(const bl_action_t *)` (enqueue at the head) · `void rt_set_tempo(int32_t bpm)` (40–220, binds at the next bar) · `void rt_load(const bl_project_t *)` (playing: a new engine from the next bar, ticks continue, mutes cleared, the template's tempo adopted) · `void rt_set_mute(int voice, int on)` · `void rt_set_channel(int voice, uint8_t ch)` · `void rt_set_sync(int ext)` (stopped only) · `void rt_set_ext_port(int mask)` (USB, TRS or both) · `const bl_live_t *rt_engine(void)` · `void app_tick(void)` (1 kHz) · `void app_loop(void)` (render ahead).
- Behaviour, mirroring `Transport.swift` and `EventConversion.swift`: render windows from the head to `min(now + 100 ms in ticks, next pending bar boundary)`, at most 240 ticks each; notes through `Ratchet.expand` (slice 240/ratchet, sub-gate `max(1, min(slice·9/10, gate))`), muted voices dropped, offs at `at + gate`; CC always (mute does not drop CC); MIDI clock every 40 ticks when INT; Start at play, Stop at stop (INT only); a timed event queue (capacity 1024, ordered by tick, stable) drained by `app_tick`; a note registry (16×128) whose flush sends note-offs for sounding notes plus CC 123 per touched channel (stop, mute, channel change, arranger stop at its tick); EXT: follow F8 from the chosen port (each pulse = 40 ticks; between pulses advance at the last measured rate, never past the next pulse), FA/FB start, FC stop; no pulse for > 500 ms holds the last rate.
- `app_host_cmd` `dumpmidi FILE` (start/stop logging `tick status d1 d2` for every message sent, in send order) · `app_host_cmd` `muteat VOICE TICK 0|1` (a test hook: the mute changes for windows that start at or after TICK, the rule the reference's `midi` mode uses too).

- [ ] **Step 1:** `reftool midi`: for `midi_full.script` (full ensemble, its seed, 64 bars, `window 240`, transpositions at bars 8 and 16, `mute` of bedPluck2 from tick 30720 to 46080 and of pad from 61440), print what the runtime sends: per window the `NoteEventConverter.events` (muted voices dropped) and `ccEvents`, plus `clockTicks`, merged in a stable sort by tick, one `tick status d1 d2` line each. Write the `.ref`.
- [ ] **Step 2: Write the failing scenarios:** `play_midi.bln` (`app_host_cmd` `selfscript tests/equiv/midi_full.script` sets the template, seed and transpositions as rt actions at their ticks and the mutes with `muteat`; PLAY; `dumpmidi`; wait the 64 bars; `app_host_cmd` `cmpmidi tests/equiv/midi_full.ref` compares the dump line by line); `stop_flush.bln` (ambient drift, play 20 bars, stop mid-drone: `expect_out USB 80 …` note-offs for the drone's two notes and `B0 7B 00` on its channel; same for mute and for a template switch); `ext_clock.bln` (EXT, `midi FA`, 96 × `midi F8` 20 ms apart, then none for 2 s: `expect tick_rate` unchanged and playing 1, then `midi FC`: playing 0); `tempo.bln` (tempo change mid-bar binds at the bar line: `expect` the tick of the change).
- [ ] **Step 3:** Run. Expected: FAIL.
- [ ] **Step 4:** Implement `runtime.c` (locks between `app_tick` and the main loop with `plat_lock`/`plat_unlock`).
- [ ] **Step 5:** Run. Expected: the four scenarios pass.
- [ ] **Step 6:** Commit: "Runtime: the transport, MIDI out, clock in and out".

### Task 14: The saved project, slots and undo

**Files:**
- Modify: `app/project.c` (create), `app/app.c`, `app/host.c`
- Create: `tests/scenarios/persist.bln`
- Test: `tests/host/engine_test.c` (`test_state_pack`, `test_store_bad_magic`)

**Interfaces:**
- Consumes: Tasks 11, 13.
- Produces: `typedef struct { uint32_t magic, format; uint8_t tpl, key_root, key_mode, sync, ext_ports, lights; uint64_t seed; int32_t bpm; uint8_t arr_form; /* 0 off, 1 classic, 2 slow, 3 ambient */ uint8_t density_pct, has_density; uint8_t channel[BL_MAX_VOICES]; } proj_t;` (`PROJ_MAGIC 0x314E4C42` "BLN1", `PROJ_FORMAT 1`) · `extern proj_t proj;` · `void proj_defaults(void)` (Full ensemble, A minor, a fresh seed) · `uint64_t proj_fresh_seed(void)` (one SplitMix64 round over `plat_ms()` and `plat_master()` at the moment it is asked: template load and Dice, as the Mac's `UInt64.random`) · `int proj_load(void)` · `int proj_save(void)` (project + the six slots) · `void proj_to_engine(bl_project_t *out)` (the app project as Berlinan's `Project`: template + seed + key + bpm + arranger + density + channels) · slots: `int slot_store(int k)` (capture at the head) · `int slot_recall(int k)` · `int slot_used(int k)` · undo: `void undo_push(void)` (before every destructive action: regenerate, reroll, recall, Dice; 20 deep, the oldest dropped) · `int undo_pop(void)` · `int state_pack(const bl_state_t *, uint8_t *buf, int max)` / `int state_unpack(const uint8_t *, int n, bl_state_t *out)` (u16 positions and gates, i8 pitch fields, one byte of flags; a snapshot never exceeds `PLAT_STORE_MAX` for the three templates).
- The app project and the running engine are separate, as on the Mac (`transportStale`): a change made while playing that the Mac defers (tempo, Dice's seed, density, arranger on/off) goes into `proj` at once and reaches a new engine at the next Play.

- [ ] **Step 1: Write the failing tests:** `test_state_pack` (pack/unpack roundtrip of 200 states captured from the equivalence scripts; the largest packed size printed and ≤ 3840) · `test_store_bad_magic` (a record with another magic, a wrong format, a short record: `proj_load` returns non-zero and `proj` equals `proj_defaults` apart from the seed) · `persist.bln` (change template, key, tempo, a channel, store slot 2, SAVE, `reboot`: the same values, slot 2 lit; recall slot 2 after reboot plays the stored patterns: `expect` a pattern hash).
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run. Expected: pass.
- [ ] **Step 5:** Commit: "Project: SAVE, the six slots and undo, as a Berlinan document".

### Task 15: The panel: keys, buttons, encoders, LEDs

**Files:**
- Modify: `app/ui.c` (create), `app/app.c`, `app/host.c`
- Create: `tests/scenarios/keys.bln`, `tests/scenarios/buttons.bln`, `tests/scenarios/encoders.bln`, `tests/scenarios/help.bln`

**Interfaces:**
- Consumes: Tasks 13, 14.
- Produces: `void ui_init(void)`, `void ui_frame(void)`, `void ui_input_only(void)`, `void ui_say(const char *, const char *)`; host `expect` names: `view`, `voice_sel`, `degree`, `pending_degree`, `muted<V>`, `active<V>`, `locked<V>`, `density_pct`, `tpl`, `bpm`, `mode`, `root`, `slot_used<K>`, `tension_left`, `arr_on`, `arr_phase`.
- The mapping is the spec's [Controls](../specs/2026-10-26-berlinan-fm1-design.md#controls) tables, exactly: keys 0–6 (F3–B3) the voices in role order Pluck 1, Pluck 2, Bass, Pad, Lead, Drone, Heartbeat (a role missing from the template: key dark, inert); white keys C4–B4 = `PLAT_WHITE_KEYS[4..10]` degrees 1–7; black C#4 D#4 F#4 G#4 A#4 = `PLAT_BLACK_KEYS[3..7]` scenes 1–5; C5 C#5 D5 D#5 E5 F5 = keys 19–24 slots 1–6; F#5 (key 25) `>>`; G5 (key 26) tension. A voice key held + EDIT / REC / SEL = regenerate / blank / lock that voice; tap = mute toggle, or `setVoiceActive(on)` when the voice is WAITS. MASTER: density in steps of 1 % (`plat_master() * 100 / 4096`), sent only when the step changes and held for 150 ms. Every action goes through the same path as the Mac's (`rt_action`, `proj`, `undo_push` where `UIState` pushes). Help card on a 1 s hold of any button (AMB-1's mechanism and wording style).
- Values the Mac cannot change live are shown, not editable (spec, Pages).

- [ ] **Step 1: Write the failing scenarios** (one `expect` per mapping row; e.g. `keys.bln`: `tapkey 2` → `expect muted2 1`; `tapkey w5` → `expect pending_degree 2`, wait to the bar → `expect degree 2`; `key 2 down` + `tap EDIT` + `key 2 up` → the bass's generation +1 at its loop boundary; SAVE + `tapkey 20` → `expect slot_used1 1`; MASTER 2048 → `expect density_pct 50`).
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement the input half of `ui.c`, mirroring AMB-1's `ui.c` structure.
- [ ] **Step 4:** Run. Expected: pass.
- [ ] **Step 5:** Commit: "Panel: the keys, buttons, encoders and MASTER as Berlinan's controls".

### Task 16: The screens

**Files:**
- Modify: `app/ui.c`, `app/words.tsv`, `app/words.allow`
- Create: `tests/scenarios/pages.bln`, `tests/scenarios/reader.bln`

**Interfaces:**
- Consumes: Task 15, `bl_live_status`.
- Produces: the seven pages of the spec's Pages table (HOME, ENV, ARP, LFO, SEQ, FX, GLO), drawn with fm1-base's `gfx.c` in AMB-1's look; the FX page computes `DelayHint` (dotted 8th on beds and heartbeat, 8th on bass and drone, quarter on pad and lead; `60000 / bpm × beats`, rounded) per voice; the GLO page shows the seed as 16 hex digits; every text on the screen in `words.tsv`; `app_speak_dump` for the reader.

- [ ] **Step 1: Write the failing scenarios:** `pages.bln` (each page button, `shot` each page, `expect view`), `reader.bln` (`listen`, page changes and a knob turn: `said` the page name and the value).
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement the drawing and the words.
- [ ] **Step 4:** Run. Expected: pass; look at every screenshot in `app/build/scenarios/pages/` and check the text fits (no clipped labels).
- [ ] **Step 5:** Commit: "Screens: the seven pages, the horizon and the screen reader's words".

### Task 17: Manual and the full check

**Files:**
- Create: `docs/MANUAL.md`
- Modify: `README.md`

- [ ] **Step 1:** Write `docs/MANUAL.md` (AMB-1's manual structure: what it is, getting started with a MIDI router, controls, pages, saving, sync, recovery) and finish `README.md` (link the spec, the reference tool, how equivalence is tested).
- [ ] **Step 2:** Run `sh tests/run_tests.sh` and `./build.sh`. Expected: `all tests passed`; the build's checks clean.
- [ ] **Step 3:** Commit: "Manual, and the README's last word".
