# The equivalence format

The port must render what Berlinan's engine renders: the same template, seed and actions at the same ticks, in the
same windows, give the same `RenderedNote`s and `RenderedCC`s, field for field, in the same order. This directory holds
the scripts (`*.script`) and what the unchanged Swift engine renders for them (`*.ref`), committed so that the tests
never need Swift. `tests/host/equiv_test.c` plays each script on the C engine, prints the same lines and compares them
with the `.ref` line by line.

The `.ref` files come from `tools/reference/` (RefTool):

- Berlinan commit `0e4a7195cefb9aed5f21ee46bd6eddeebeb776ac` (v1.2.9), extracted by `tools/reference/fetch.sh` with
  `git archive` (nothing is written into the Berlinan checkout) and compiled unchanged: `Sources/BerlinanEngine` and,
  for the runtime's MIDI conversion, `EventConversion.swift`, `MIDI1Event.swift`, `NoteRegistry.swift`.
  `Sources/Berlinan/TemplateStore.swift` is extracted for reference only (its `defaultKey`, A natural minor).
- Apple Swift 6.3.3 (`swift-driver version: 1.148.6 Apple Swift version 6.3.3 (swiftlang-6.3.3.1.3
  clang-2100.1.1.101)`, target `arm64-apple-macosx26.0`), release build. The standard library's random algorithms are
  part of the reference, so `make_refs.sh` refuses any other Swift version.

Regenerate everything with `tools/reference/make_refs.sh` (it fetches, builds, runs `RefTool selftest`, then rewrites
every `.ref`). One script by hand:

    swift run -c release --package-path tools/reference RefTool script tests/equiv/smoke.script

## Scripts

A script is UTF-8 text, one statement per line. `#` starts a comment that runs to the end of the line; blank lines are
ignored; words are separated by spaces or tabs. Integers are decimal with an optional `-`; seeds are `0x` and 1 to 16
hex digits (either case).

### The project (before play)

Each of these at most once, anywhere in the file. `template`, `window` and `bars` are required.

| Line | Meaning |
| --- | --- |
| `template demo\|full\|ambient` | `Project.demo`, `Project.fullEnsemble`, `Project.ambientDrift`, each with its key set to A natural minor (`Key(root: 9, scale: .preset(.naturalMinor))`), as `TemplateStore.inDefaultKey` does. |
| `seed 0xHEX` | `masterSeed`. Without it, the template's own seed (`0xBE1271A9`, `2477861893`, `0x0A3B11ED`). |
| `arranger off\|classic\|slow\|ambient` | `classic`, `slow`, `ambient`: `arrangement = .classicArc`, `.slowBuild`, `.ambientPlateau` (as `AppServices.setArrangerForm`). `off`: `(arrangement ?? ArrangementConfig())` with `enabled = false` (as `AppServices.setArrangerEnabled(false)` while stopped). Without it, the template's own (`ambient`: `.ambientPlateau`; the others none). |
| `density K` | `density = max(0, min(1, Double(K) / 100))`, K in 0–100. Without it, none (1.0). |
| `window T` | The render window in ticks, T ≥ 1. |
| `bars N` | Length: N bars of 4/4 = N × 3840 ticks (960 PPQN; every template is 4/4). |

The order of application: the template, its key, the seed, the arranger, the density. The engine is
`LiveEngine(project:)` of the result.

### Actions

`at TICK ACTION…`: one action per line. TICK is in transport ticks, 0 ≤ TICK < bars × 3840, and never smaller than the
previous `at` line's. `V` is a voice id of one of the templates (`bedPluck1`, `bedPluck2`, `bass`, `pad`, `lead`,
`kick`, `drone`); an id the running engine does not have does nothing, as in the engine.

| Action | What the Mac does (and the tool) |
| --- | --- |
| `transpose D` | `.transpose(degreeOffset: D)` (`UIState.pressDegree`: degree key k sends D = k − 1, 0–6; any integer is accepted). |
| `regenerate V` | `.regenerate(voiceID: V)`. |
| `reroll` | `UIState.rerollAllUnlocked`: `.regenerate` for every voice of the engine's snapshot whose pattern is not locked, in project order. |
| `blank V` | `.blankCycle(voiceID: V)`. |
| `lock V 0\|1` | `.setLock(voiceID: V, on: false\|true)`. |
| `save SLOT` | Slot SLOT (0–31) = the engine's `captureState()`. |
| `recall SLOT` | `.recallSnapshot(slot)`; an empty slot does nothing (`UIState.recallSnapshot`). |
| `key ROOT MODE` | `AppServices.applyLiveMusicalChanges`: the app project's key = `Key(root: ROOT, scale: .preset(MODE))`, then `.setKey` of it. ROOT 0–11; MODE a `ModeID` raw value other than `custom`: `naturalMinor`, `dorian`, `phrygian`, `mixolydian`, `lydian`, `lydianDominant`, `harmonicMinor`, `minorPentatonic`, `locrian`, `major`. |
| `density K` | `AppServices.setDensity` while playing: v = `max(0, min(1, Double(K) / 100))`, K 0–100; the app project's density = v, then `.setDensity(v)`. |
| `bpart` | `.fireBPart`. |
| `voice V on\|off` | `.setVoiceActive(voiceID: V, on:)`. |
| `tension` | `UIState.spendTension`: `.spendTension` on the app project's first voice with role `pad`, else its first voice. |
| `scene N` | `UIState.morphToScene`: `.morphToScene(name: scenes[N].name, bars: 8)`, `scenes` the app project's `ccScenes` (0-based: ambient's `closed` 0, `open` 1); N past the end does nothing (the Mac offers no such scene). |
| `arranger on\|off` | `AppServices.setArrangerEnabled` while playing: the app project's `(arrangement ?? ArrangementConfig())` gets `enabled`, then `.setArrangementEnabled`. |
| `advance` | `.advancePhase`. |
| `reseed 0xHEX` | `AppServices.reseed` while playing: p = the app project with `masterSeed` = HEX; `.recallSnapshot(LiveEngine(project: p).captureState())`; the app project becomes p. The running engine keeps its own project (and its old seed). |
| `load demo\|full\|ambient 0xHEX` | `AppServices.applyTemplate` while playing: p = the template (in A minor, as above) with `masterSeed` = HEX, nothing else of the current setup; the app project becomes p and `Transport.loadProject(p)` arms it. |

**The app project.** As on the Mac, the app's project (`AppServices.project`) is not the running engine's: it starts as
the script's project, and `key`, `density`, `arranger`, `reseed` and `load` change it as the table says. `tension`,
`scene` and `reseed` read it, so after a `reseed` the app's seed differs from the running engine's, and a later
`reseed` builds its fresh engine from the app project as it is then (its key, density and arranger included).

### The run

The tool plays the script as Berlinan's `Transport` scheduler loop plays it, with a render head `h` (transport ticks)
from 0 to bars × 3840. One step:

1. **The actions at `h`**, in script order, as the app performs them (the tables above). Engine actions are not given
   to the engine yet; they wait in a list (`Transport.pendingActions`). Reads of the engine (`reroll`'s locks, `save`)
   see the engine as the last window left it, as the Mac's mirror does.
2. **The wake.** If a project is armed: its boundary b = `origin + ceil((h − origin) / 3840) × 3840` (the next bar line
   of the running engine, `Transport.nextBar`). If `h` = b, a fresh `LiveEngine(project:)` of it takes over, `origin`
   = b, nothing is armed any more. Then every waiting action, in order: `engine.enqueue(action, atTick: h − origin)`.
3. **The window** `[h, e)`, e the smallest of: the next multiple of `window` after `h` (the grid is absolute: k ×
   `window`), the next `at` tick after `h`, the armed project's boundary, bars × 3840. The engine renders
   `render(range: (h − origin)..<(e − origin))`, then `renderCC` of the same range.
4. **The lines** of the window (below), then `h` = e.
5. **The ending.** If the engine has a `pendingStopTick` s and e ≥ s + `origin`, the run ends (the transport stops).

`origin` (where the running engine's tick 0 sits) is 0 until a `load` takes over. Output ticks are transport ticks:
engine ticks + `origin`.

## Reference lines

One line per item, words separated by one space, every line ending in `\n`, no comments. Per window, in this order:

| Line | When |
| --- | --- |
| `N <at> <voiceID> <midi> <vel> <gate> <accent> <ratchet>` | Every `RenderedNote` of `render`, in the order it returned them. `at` = `note.at + origin`; `accent` 0 or 1. (`isEcho` is not written: no voice has a MIDI delay.) |
| `C <at> <voiceID> <controller> <value>` | Every `RenderedCC` of `renderCC`, in the order it returned them; `at` + `origin`. |
| `S <tick>` | Once per engine, after the first window in which its `pendingStopTick` is set: `pendingStopTick + origin`. |
| `H <tick> <hash>` | When the window's end crosses a multiple of 16 bars (61440 ticks): `h / 61440 < e / 61440`, integer division. `tick` = e; `hash` = the state hash of the running engine after the window, 16 lower-case hex digits. |

All integers in decimal. Notes come before CC within a window, so the interleaving of `N` and `C` lines depends on the
window size, and so do the `H` lines; each window size is its own script and its own `.ref`.

## The state hash

FNV-1a 64 (offset basis `0xcbf29ce484222325`, prime `0x100000001b3`, over bytes) over a canonical text in UTF-8 built
only from the engine's public API: `snap = engine.snapshot()` and `engine.captureState().generations`
(`tools/reference/Sources/RefTool/Dump.swift`). One line per item, words separated by one space, every line ending in
`\n`; booleans `0`/`1`, integers in decimal (seeds as unsigned decimal).

1. Per voice of `snap.voices` (project order):
   `voice <id> <active> <blankActive> <pending> <generation> <length> <startOffset> <locked> <lifecycle> <seed>`
   - `pending`: `snap`'s `pendingRequest` as `<kind> <tick>` (kind `blank`, `regenerate`, `replace` or
     `melodyToggle`, tick in engine ticks), or `-` when there is none.
   - `generation`: `captureState().generations[id]`.
   - `length`, `startOffset`, `locked`: of the voice's `pattern`.
   - `lifecycle`: the `remaining` of `.repeating(remaining:)`, or `E` for `.established`.
   - `seed`: `pattern.provenance.seed`.

   then per note of `pattern.notes`, in their order:
   `note <position> <degree> <octave> <accidental> <velocity> <gate> <accent> <ratchet>` (`degree`, `octave`,
   `accidental` of `pitch`).
2. Per event of `snap.scheduleEvents`: `sched <bar> <degreeOffset>`.
3. `key <root> <offset>…`: `snap.key.root`, then each of `snap.key.scale.offsets`.
4. `density <16 hex digits>`: the bit pattern of `snap.density` (IEEE binary64), lower case.
5. `tension <snap.tensionRemaining>`.
6. `arr <phase> <barsInPhase> <barsUntilChange> <id>…` of `snap.arrangement` (`phase` its raw value: `build`, `bed`,
   `variation`, `bPart`, `payoff`, `subtract`, `ending`; then `activeVoices` in their order), or `arr -` when nil.
7. Per event of `snap.upcomingEvents` (the engine's order): `up <tick> <kind> <detail> <voiceID>`. `kind` the raw
   value (`key`, `recall`, `density`, `bPart`, `voiceActive`, `alternating`, `tension`, `promoteToLead`, `arranger`,
   `advance`, `scene`); `detail` as the engine formats it, `-` when empty: the key's root in decimal; for density
   Swift's description of `(value * 100).rounded() / 100` (shortest round-trip form with at least one decimal:
   `0.35`, `1.0`, `0.0`; `rounded()` rounds half away from zero); `on`/`off`; a voice id; a scene name. `voiceID` or
   `-`.

The text has no trailing separator besides the last line's `\n`. The `note` lines follow their `voice` line; nothing
else is in the text (pattern ids, ties, gate cycles, MIDI delay and melody state are not part of it).
