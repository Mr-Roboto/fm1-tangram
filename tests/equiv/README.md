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
`kick`, `drone`). The running engine need not have it (see "Ids the running engine does not have" below).

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
| `load demo\|full\|ambient 0xHEX` | `AppServices.applyTemplate` while playing: p = the template (in A minor, as above) with `masterSeed` = HEX, nothing else of the current setup; the app project becomes p and `Transport.loadProject(p)` arms it. The template's tempo changes only the transport's clock (wall time per tick), never the ticks, so it is not part of the reference. |

**Ids the running engine does not have.** A script may name a voice or scene the running engine lacks: `voice pad on`
on the demo trio, or a `tension` or `scene` between a `load` and its bar line (they read the app project, already the
new template, while the old engine still runs). The engine then does what `LiveEngine.enqueue` does, action by action:

- `regenerate`, `blank`, `lock`: nothing at all (`withState` finds no voice).
- `voice V on|off`: queued as a bar-bound change like any other; until it binds it is an `up … voiceActive on|off V`
  line of the state text; when it binds, nothing changes.
- `tension`: queued; until it binds, an `up … tension V V` line; when it binds it still spends one unit
  (`tensionRemaining` drops by one if it is above 0), and the one-shot note is dropped without sounding when its window
  renders, because the engine has no such voice.
- `scene N`: queued with the app project's scene name; until it binds, an `up … scene NAME -` line; when it binds,
  nothing if the engine's project has no scene of that name. If it has one, every `voiceID|target` of that scene gets a
  morph, including voices the engine lacks; those never emit CC (`sceneEmissions` skips them).
- `reseed` and `recall` of a state captured from another project: `applyRecall` takes the schedule, takes the pattern
  and generation of each voice whose id is in the state, and leaves the other voices' patterns and generations as they
  are (all voices re-align and leave blank, regenerate and melody requests).

In a script, `tension` and `scene` reach an engine without that voice or scene only between a `load` and its bar line.
They bind on that same bar line, where the new engine has already taken over, so they show only as `up` lines in an
`H` state of the old engine. The C engine must still apply the effects above, because it is checked against the engine
and not only against these scripts.

So the C runner must be able to keep, in its queued changes, a voice id (and a scene name) that its engine does not
have, write it into the state text, and apply it with the engine's effect above.

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

### Debugging a mismatch

`RefTool script --text FILE` writes the same lines, except that each `H` line is replaced by the canonical state text
it would hash (below). Each line of that text is prefixed with `T `, so the C runner can print the same text and the
two can be diffed. To get the `H` hash back from a block of `T` lines, drop each `T ` prefix, end every line with `\n`,
and hash the result. `equiv_test --text FILE` writes the C engine's lines the same way.

### The live scripts

`tests/host/equiv_test.c` plays every `live_*.script` on the C engine (`app/engine/live.c`) through
`tests/host/script.c`, the same run in C, and compares its lines with the `.ref`. Per script it prints `ok` or `FAIL`
and `notes-hash <16 hex digits>`, FNV-1a 64 (as the state hash) over the script's `N` lines, each with its `\n`; the
app's own render (Task 12) is checked against that value. On the first difference it prints the line number, both
lines and the last `H` line that matched. Each script is there three times, in windows of 240, 960 and 37 ticks (the
suffix `_240`, `_960`, `_37`): the notes and the state are the same, the windows split differently (37 splits every
change and every bar line inside a window).

| Scripts | What they play |
| --- | --- |
| `live_plain_{demo,full,ambient}` | The template on its own seed, 512 bars, no actions (the ambient's arranger reaches its ending and stops the run). |
| `live_seeds_{demo,full,ambient}` | The template with 8 seeds, 256 bars each: the first from the `seed` line, each next one loaded on its bar line. |
| `live_actions_{demo,full,ambient}` | Every action three times or more over 400 bars, on and off bar lines, some pending at a 16-bar line (in an `H` state), `bpart` twice in a bar, keys to other modes (pentatonic too: another scale size) and back, densities 35, 100, 0 and 100, saves and recalls, reseeds, the arranger on and off, ids the running engine lacks, and three loads (the last back to the template). |
| `live_arranger_{classic,slow,ambient}` | The full ensemble under the arranger preset, 800 bars, to the ending and its stop. |
| `live_arranger_toggle` | The full ensemble, the arranger switched on at bar 20 (it adopts the ensemble: the bed), three advances, the bass locked after a nudge, the arranger off at bar 128 and on again at bar 208 (over the voices left playing: a build). Off at 128 and not later: left on, the arranger reaches its ending at bar 175. |

The C engine's queues are bounded (`app/engine/live.h`) and the run's buffers too (`script.c`); a script that goes
past them is a failure of the run (`the engine's queue is full`), never a silent difference.

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
     `melodyToggle`), or `-` when there is none. The tick is the engine's (no `origin` added).
   - `generation`: `captureState().generations[id]`.
   - `length`, `startOffset`, `locked`: of the voice's `pattern`.
   - `lifecycle`: the `remaining` of `.repeating(remaining:)`, or `E` for `.established`.
   - `seed`: `pattern.provenance.seed`.

   then per note of `pattern.notes`, in their order:
   `note <position> <degree> <octave> <accidental> <velocity> <gate> <accent> <ratchet>` (`degree`, `octave`,
   `accidental` of `pitch`).
2. Per event of `snap.scheduleEvents`: `sched <bar> <degreeOffset>`; the bar counts from the running engine's tick 0
   (no `origin` added).
3. `key <root> <offset>…`: `snap.key.root`, then each of `snap.key.scale.offsets`.
4. `density <16 hex digits>`: the bit pattern of `snap.density` (IEEE binary64), lower case.
5. `tension <snap.tensionRemaining>`.
6. `arr <phase> <barsInPhase> <barsUntilChange> <id>…` of `snap.arrangement` (`phase` its raw value: `build`, `bed`,
   `variation`, `bPart`, `payoff`, `subtract`, `ending`; then `activeVoices` in their order), or `arr -` when nil.
7. Per event of `snap.upcomingEvents` (the engine's order): `up <tick> <kind> <detail> <voiceID>`, the tick in
   engine ticks (no `origin` added). `kind` the raw
   value (`key`, `recall`, `density`, `bPart`, `voiceActive`, `alternating`, `tension`, `promoteToLead`, `arranger`,
   `advance`, `scene`); `detail` as the engine formats it, `-` when empty: the key's root in decimal; for density
   Swift's description of `(value * 100).rounded() / 100` (shortest round-trip form with at least one decimal:
   `0.35`, `1.0`, `0.0`; `rounded()` rounds half away from zero); `on`/`off`; a voice id; a scene name. `voiceID` or
   `-`.

The text has no trailing separator besides the last line's `\n`. The `note` lines follow their `voice` line; nothing
else is in the text (pattern ids, ties, gate cycles, MIDI delay and melody state are not part of it).

## The templates

`tests/equiv/templates.ref` is `RefTool templates`: the three templates as the Mac instantiates them
(`TemplateStore.inDefaultKey`: A natural minor, each with its own seed, nothing else changed), for checking the port's
templates field by field. For each template, in the order demo, full, ambient: a comment line `# template <name>`, then
one line per item. Words are separated by one space, integers are decimal, a `Double` is the 16 lower-case hex digits
of its bit pattern, booleans are `0`/`1`, and `-` marks an optional that is nil.

| Line | Field |
| --- | --- |
| `seed <masterSeed>` | unsigned decimal |
| `bpm <bits>` | `bpm` |
| `key <root> <offset>…` | `key.root`, `key.scale.offsets` |
| `meter <beatsPerBar> <beatUnit> <grouping>…` | `meter` |
| `event <bar> <degreeOffset>` | one per event of `transposition.events` (none: no line) |
| `arrangement <enabled> <minPeriodBars> <maxPeriodBars> <ambientMode> <reprise> <buildOrder>…` or `arrangement -` | `reprise` 0/1/`-`; `buildOrder` its ids, or `-` when nil |
| `density <bits>` or `density -` | `density` |
| `bpart <barsPerStep> <swapBass> <dropBed> <progressionSteps>…` or `bpart -` | `bPart` |
| `tension <n>` or `tension -` | `tensionBudget` |
| `legitimization <repeats> <ratchetProbability bits or ->` or `legitimization -` | `legitimization` |
| `velocity <accentBoost> <humanizeRange>` or `velocity -` | `velocity` |
| `slides <0\|1>` or `slides -` | `slidesEnabled` |
| `unported <harmony> <alternating> <snapshots>` | each `set` or `-`: fields the port leaves out, which must be `-` |
| `voice <id> <role> <low> <high> <referenceC> <channel> <phaseSteps> <anchorPolicy> <participates>` | one per voice in project order; `role` and `anchorPolicy` raw values; `phaseSteps`, `anchorPolicy`, `participatesInTransposition` `-` when nil |
| `generator <case> <params>…` | after its voice: `seedProcedure <bars> <mutations>`, `pulsingBass <accents>`, `stagedPluck <bars>`, `bassMelody <bars>`, `chordLayer <periodBars>`, `heartbeat`, `drone <bars>` (also `highMidLow <register>`, `drunkardsWalk <bars> <scatter>`, `arpeggiator <octaves> <direction> <rate> <degrees>…`, which no template uses) |
| `lane <target> <shape> <cycleBars> <depth> <center> <phase> <preStrikeTicks>` | one per `ccLanes` entry, in order; `shape` its raw value, the four Doubles as bits |
| `ccmap <target> <controller>` | one per `ccMap` entry, sorted by target |
| `unported <gateRamp> <accumulator> <overridePattern> <midiDelay>` | the voice's last line; each `set` or `-` |
| `scene <name>` | one per `ccScenes` entry, in order, followed by its `value <voiceID> <target> <bits>` lines, sorted by voice id, then by target |
| `phase <phase> <scene>` | one per `sceneByPhase` entry, sorted by phase |

The engine reads a nil `ccLanes`, `ccMap`, `ccScenes` or `sceneByPhase` as empty, so in this file both give no lines.

## The random draws

`tests/equiv/rng.ref` is `RefTool rng`: what the Swift 6.3.3 standard library draws from a `SplitMix64(seed:)`, for
`app/engine/rng.c`. Each line starts with a word and a seed (unsigned decimal; a fresh generator per line), integers
are decimal, a `Double` is the 16 lower-case hex digits of its bit pattern; `#` lines are comments.

| Line | Content |
| --- | --- |
| `next <seed> <v>…` | the first 64 `next()` of the generator |
| `derive <master> <result> <label>…` | `SeedStream.derive(master:, labels:)`; the labels are the rest of the line (none contains a space) |
| `in <seed> <lo> <hi> <v>…` | 2,000 successive `Int.random(in: lo...hi)` |
| `upto <seed> <lo> <hi> <v>…` | 2,000 successive `Int.random(in: lo..<hi)` |
| `bool <seed> <v>…` | 2,000 successive `Bool.random()`, `1` for true |
| `unit <seed> <bits>…` | 2,000 successive `Double.random(in: 0..<1)` |
| `index <seed> <count> <v>…` | 500 successive `randomElement` indices of an array of `count` elements |

## The patterns

`tests/equiv/gen.ref` is `RefTool gen` (`tools/reference/Sources/RefTool/Gen.swift`): the patterns of the generators
the templates use, made by `PatternFactory.make` and `PatternFactory.makeBassMelody`, and the pattern ops on some of
them, for `app/engine/pattern.c` and `app/engine/gen.c`. Integers are decimal (seeds unsigned), a `Double` is the 16
lower-case hex digits of its bit pattern, booleans are `0`/`1`; `#` lines are comments.

The cases, in this order: `seedProcedure(1, 4)` as `bedPluck1`, `bedPluck2` and `bass` (the bass is the procedure's
other branch; the two beds give the same patterns), then `seedProcedure(1, 3)` the same three ways, `pulsingBass(3)` as
`bass`, `stagedPluck(2)` as `lead`, `bassMelody(16)` as `bass` (through `make`: the pedal), `chordLayer(7)` as `pad`,
`heartbeat` as `rhythm`, `drone(8)` as `drone`; each in A natural minor (`Key(root: 9, scale: .preset(.naturalMinor))`),
then in C natural minor (`Key.default`); each with the 200 seeds `SeedStream.derive(master: k, "gen")`, k = 0..<200.
Then two configs no template uses, in A minor only, as `bedPluck1`: `seedProcedure(1, 16)` and `seedProcedure(2, 8)`
(more mutations, so that a degree move reaches the clamp at 4). Then `makeBassMelody(bars: 16)` over two schedules, with the same 200 seeds each: the full ensemble's
(`HarmonyScheduler.resolve(project: .fullEnsemble).schedule`, the demo's transposition) and a busy one, a change every
bar of up to 14 degrees, so that the walk also takes its fallback steps. Meter 4/4 throughout.

`<notes>` below is `<n>` and then per note of `pattern.notes`, in their order,
`<position> <gate> <degree> <octave> <accidental> <velocity> <accent> <ratchet>`.

| Line | Content |
| --- | --- |
| `gen <kind> <a> <b> <role> <root> <seed> <length> <startOffset> <generator> <remaining> <notes>` | a `make` case: the config (`<kind> <a> <b>` as `seedProcedure <bars> <mutations>`, `pulsingBass <accents> 0`, `stagedPluck <bars> 0`, `bassMelody <bars> 0`, `chordLayer <periodBars> 0`, `heartbeat 0 0`, `drone <bars> 0`), the role's raw value, the key's root, the seed; then the pattern: `provenance.generator`'s raw value, the lifecycle's `remaining` (or `E` for `.established`) |
| `walk <bars> <events> <bar> <offset>… <seed> <length> <notes>` | a `makeBassMelody` case: the schedule's events (sorted by bar) and the seed; then the pattern |
| `cap <density> <length> <notes>` | `PatternOps.densityCap` of the last `gen`/`walk` pattern, for each density 0, 0.01, 0.35, 0.5, 0.99, 1, 1.5, -0.5 (the last three: the identity and the clamp) |
| `roots <length> <locked> <remaining> <notes>` | `PatternOps.thinToRoots` of it |
| `accents <seed> <notes>` | `PatternOps.moveAccents` of it with that seed (`SeedStream.derive(master: i, "accents")`, i the pattern's place in the file counting from 0) |
| `occ <length> <rotate> <lo> <hi> <startOffset> <count> <at> <index>…` | `q = p.withLength(length).rotated(by: rotate)` of it: `q.startOffset`, then `q.occurrences(in: lo..<hi)`, each its tick and its note's index in `q.notes` |
| `onset <length> <rotate> <from> <step> <bits>` | for the same `q`, `q.hasOnset(atGlobalTick:)` at `from`, `from + step`, … up to the `occ` line's `hi`, one `0`/`1` character each |

The ops follow every 104th pattern of the file (the 0th, the 104th, …: 54 of the 5,600), each with a `cap` line per
density, one `roots` and one `accents` line, and two `occ`/`onset` pairs: the length 240 ticks shorter with no rotation
from tick 1234, and 480 ticks longer rotated by -1000 from tick -3000, both over two loops and 777 ticks.

## Legitimization and mutation

`tests/equiv/mut.ref` is `RefTool mut` (`tools/reference/Sources/RefTool/Mut.swift`): the lifecycle and the mutation
of `Legitimizer` (`advanceAtLoopBoundary`, `isMutable`, `mutate` with the slides off) on voices of the templates'
generators, for `app/engine/legit.c`. Integers are decimal (seeds unsigned), a `Double` is the 16 lower-case hex digits
of its bit pattern, booleans are `0`/`1`, a lifecycle is its `remaining` or `E` for `.established`, `<notes>` is as in
gen.ref; `#` lines are comments. Meter 4/4 throughout.

The voices, 25 of each of 12 cases (id, role, generator config, anchor policy; register and reference C as the
templates have them): `bedPluck1` `seedProcedure(1, 4)` none; `bedPluck2` `seedProcedure(1, 4)` phased -240 none;
`bass` `pulsingBass(3)` beats1and3; `bass` `bassMelody(16)` beats1and3; `pad` `chordLayer(7)` none; `kick` (rhythm)
`heartbeat` none; `drone` `drone(8)` none; `lead` `stagedPluck(2)` none; `bedPluck1` `seedProcedure(1, 3)` beat1;
`bedPluck2` `seedProcedure(1, 3)` phased -240 beat1; `pad` `chordLayer(7)` beat1; `bass` `pulsingBass(3)` phased -240
beats1and3. "Phased -240" is `PhasingOps.phasedCopy(newLength: length - 240)` (bedPluck2's `phaseSteps: -1`: a loop
shorter than a bar, so the anchors come from `max(bars, 1)`). Of the 25, 10 are in A natural minor, 10 in C natural
minor (`Key.default`), 5 in A minor pentatonic (5 degrees: the shifts draw from -5...5), the k-th voice of a case
(k = 0…24, counted across the three keys) made with the seed `SeedStream.derive(master: k, "gen")` (its `<master>`).

Voice i of the file (from 0) has `repeats` 4 when i is even, else 16, and `ratchetProbability` nil, 0.5, nil, 0, nil,
0.5 for i mod 6 = 0…5. Its lines: the `voice` line (the pattern fresh from `make`, `.repeating(remaining: 4)`); when
i mod 10 = 0 a `mut` of the fresh pattern (the identity: still repeating); `advance 4` (now established); when i mod 10
= 5 `lock 1`, a `mut` (the identity: locked), `lock 0`; then ten mutations as LiveEngine makes them, generation g =
0…9 with the seed `SeedStream.derive(master: <master>, "voice", <id>, "mut", String(g))`, an `advance <repeats>`
between two of them (the verbatim plays, to `.established` again). Every line applies to the voice the lines before
made. Then 20 `life` lines, each followed by its `advance`: the lifecycle from `.established` and `.repeating` 0, 1,
2, 3, 4, 5, 8, 16, 32, unlocked and locked, for `max(remaining, 0) + 2` boundaries.

| Line | Content |
| --- | --- |
| `voice <id> <role> <anchor> <low> <high> <referenceC> <participates> <root> <mode> <kind> <a> <b> <master> <phase> <length> <startOffset> <locked> <lifecycle> <notes>` | a new voice: its fields (`role`, `anchor`, `mode` their raw values), the key, the generator config as in gen.ref, the seed it was made with, the phase (0: none), then the pattern |
| `mut <g> <seed> <repeats> <ratchet> <locked> <lifecycle> <length> <startOffset> <notes>` | `Legitimizer.mutate` of the voice with that seed and `Config(repeats:ratchetProbability:)` (`<ratchet>` the probability, `nil` for nil): the result's pattern |
| `advance <count> <lifecycle> <mutable>…` | `count` successive `advanceAtLoopBoundary`, each followed by its lifecycle and `isMutable` |
| `lock <0/1>` | the pattern's `locked` set so |
| `life <lifecycle> <locked> <mutable>` | the voice's pattern given that lifecycle and lock, and its `isMutable` |

## The schedule and the note pipeline

`tests/equiv/harmony.ref` is `RefTool harmony` (`tools/reference/Sources/RefTool/Harmony.swift`): the transposition
schedule (`TranspositionSchedule(events:)`, `offset(atBar:)`), `HarmonyEngine.quantizedEventBar`, the default
`VelocityPolicy`'s `apply`, and the note pipeline of one voice as `LiveEngine.renderVoice` runs it with the density at 1
(`PatternOps.applyTies`, the identity here, then `HarmonyEngine.render(voices: [voice])`, then the velocity policy at
the note's loop position `floorMod(at + startOffset, length)` with the pattern's `provenance.seed`), for
`app/engine/harmony.c`. Integers are decimal (seeds unsigned), booleans `0`/`1`, `<notes>` as in gen.ref; `#` lines
are comments. Meter 4/4 throughout; the draws come from one `SplitMix64(seed: 0xBE1271A9)`.

The schedules: 100 event lists, list i of `i mod 41` events, each bar drawn from `0...8`, `0...40` or `0...200` (i mod
3: the narrower, the more bars come twice or more), each offset from `-7...7`. The quantized bars: 15 ticks on and
around bar lines, then 35 from `0..<2^40`. The velocities: 300 inputs, input i with the velocity i for i < 256 (every
`UInt8`, so both clamps are reached), else a drawn one, a drawn accent and seed, and a loop position from
`0..<61440` (every tenth one negative, from `-5000...-1`).

The pipeline: four passes, each a `schedule` line, a `key` line, then per voice in project order a `voice` line and,
for each window size 37, 960 and 3840 ticks, a `window` line followed by one `w` line per window of the voice that has
notes (the windows `lo..<min(lo + size, 64 bars)`, lo = 0, size, 2 size, …; a window without a `w` line renders
nothing). The passes: `demo`, `full` and `ambient`, each template's voices as `LiveEngine(project:)` materializes them
(`voiceState(id:).voice`: the gen0 pattern from the project's seed, A natural minor; the engine's own
`OfflineRenderer.materialize`, not a copy of it) against the template's schedule
(`HarmonyScheduler.resolve(project:).schedule`; the ambient's is empty, the pedal); then `busy`, the full ensemble's
voices against 64 events, one a bar, offsets drawn from `-7...7`, with the lead's `participatesInTransposition` set
false (no template has a voice left out).

| Line | Content |
| --- | --- |
| `sched <n> <bar> <offset>… <m> <bar> <offset>… <o0> … <o200>` | the input list (in order), then the schedule's `events` (sorted by bar), then `offset(atBar:)` for the bars 0…200 |
| `bar <tick> <bar>` | `quantizedEventBar(requestTick: tick, meter: .fourFour)` |
| `vel <velocity> <accent> <seed> <local> <result>` | `VelocityPolicy().apply(velocity:accent:patternSeed:localPosition:)` |
| `schedule <name> <n> <bar> <offset>…` | the pass's schedule, sorted by bar |
| `key <root> <mode>` | the pass's key, `mode` its raw value |
| `voice <index> <id> <low> <high> <referenceC> <participates> <seed> <length> <startOffset> <notes>` | a voice: its place in the project, its register, reference C and participation, its pattern's `provenance.seed`, length, start offset and notes |
| `window <size>` | the `w` lines that follow are that voice's windows of this size |
| `w <lo> <count> (<at> <midi> <velocity> <humanized> <gate> <accent> <ratchet>)…` | `HarmonyEngine.render` of the window starting at lo, `count` notes in order: `RenderedNote`'s `at`, `midiNote`, `velocity` (the pattern's), then the velocity policy's result, `gate`, `accent`, `ratchet` |

The C schedule is bounded (`BL_MAX_EVENTS`, Swift's is not); its compaction is checked in `test_sched_compaction`
against an unbounded schedule in the test itself, not against this file.

## The arrangement

`tests/equiv/arrange.ref` is `RefTool arrange` (`tools/reference/Sources/RefTool/Arrange.swift`): the arrangement state
machine (`ArrangementSM`: `init(config:voices:seed:adoptingActive:ambientPeriodBars:)`, `tickBar(locked:)`,
`advance(locked:)`, and `deriveBuildOrder(from:)`), for `app/engine/arrange.c`. Integers are decimal (seeds unsigned),
booleans `0`/`1`, a `Double` the 16 lower-case hex digits of its bit pattern; `#` lines are comments.

The configs: `classic` (`.classicArc`), `slow` (`.slowBuild`), `ambient` (`.ambientPlateau`) and `off`
(`ArrangementConfig()`, the default: disabled, 8-16 bars). The voice lists: `demo`, `full` and `ambient`, the templates'
`voices` (the SM reads only `id` and `role`), and `mix`, seven made-up voices `x` (aux), `l` (lead), `b1` (bass), `r`
(rhythm), `b2` (bass), `p2` (bedPluck2), `d` (drone): the default rank `5 + offset`, a tie, the drone last. Each case
has the seed LiveEngine gives the SM, `SeedStream.derive(master: k, "arrangement")`, and runs 400 bars: per bar 1…400,
first any lock change, then `advance()` when the bar is one of 5, 37, 38, 90, 120, 120 (twice), 200, 255, 310, 377,
then `tickBar()`, as LiveEngine runs a bar boundary. The cases, in this order:

- the grid: `classic`, `slow`, `ambient` × `demo`, `full`, `ambient` × k = 0…29 × locked `{}`, `bass`,
  `bedPluck1,pad` (a template without the voice: that one is not in the set); not adopting, ambient period 22;
- adopting (enabled mid-play): the three presets × the three templates × k = 100…104 × the template's every voice,
  `bass,pad` (of which a template may lack one) and the empty set;
- the ambient period: `ambient` × the three templates × k = 200…202 × the periods -5, 0, 3, 7, 8, 9, 30 (below 8 the
  SM takes 8);
- every voice locked (`classic`), and `off` (tickBar does nothing, advance still works), the three templates × k =
  300…304;
- the locks changing: `classic` and `ambient` × the three templates × k = 500…504 × two schedules: everything locked,
  from bar 60 nothing, from bar 130 everything; and nothing, from bar 40 everything, from 70 `bass`, from 100 nothing,
  from 160 `bedPluck1,pad`;
- `mix`: the four configs × k = 400…409 × unlocked, `b1,b2` locked, and adopting `x,l,b2,d`.

A set is its ids in project order joined by commas, `{}` when empty; `-` is nil (not adopting). A state is
`<phase> <barsInPhase> <barsUntilChange> <activeVoices>`: the phase's raw value, the active voices in their order
(the list is ordered: build order, then in the order the voices joined), `-` when none. Directives are words after
the state, in order: `activate:<id>`, `deactivate:<id>`, `nudge:<id>` (`nudgeMutation`), `accent:<id>`
(`accentMove`), `blank:<id>` (`blankCycle`), `density:<bits>` (`setDensity`), `bpart` (`fireBPart`), `thin:<id>`
(`thinToRoots`), `stop` (`requestStop`).

| Line | Content |
| --- | --- |
| `preset <name> <enabled> <ambientMode> <reprise> <minPeriodBars> <maxPeriodBars>` | a config (`reprise` nil is 0) |
| `voices <list> (<id> <role>)…` | a voice list in project order, `role` its raw value |
| `order <list> <id>…` | `deriveBuildOrder(from:)` of that list |
| `case <list> <config> <k> <seed> <ambientPeriodBars> <adopt> <locked>` | a new SM: `ArrangementSM(config:voices:seed:adoptingActive: adopt, ambientPeriodBars:)`, locked the set its calls get |
| `init <state>` | the SM after `init` |
| `lock <bar> <locked>` | from this bar on (before its advance), the calls get this locked set |
| `a <bar> <state> <directive>…` | `advance(locked:)` at the bar: the state after it and its directives |
| `t <bar> <state> <directive>…` | `tickBar(locked:)` at the bar, when it is not part of a run |
| `q <n>` | n bars of `tickBar` with no directive and only the clock moving: barsInPhase one up, barsUntilChange one down |
| `z <n>` | n bars of `tickBar` that change nothing (`off`, or after the stop) |

Only the SM's public fields are written (`phase`, `barsInPhase`, `activeVoices`, `barsUntilChange`); the private ones
are seen through the directives: `buildOrder` (also in `order`), `density` (each `density:` value: the payoff's 1, the
subtract's `max(0.3, density * 0.85)`), `eventsInPhase` (the dwell: when a phase moves on), `roundRobin` (which voice
each nudge, accent or blank names), `stopRequested` (the `stop`, then `z` runs and empty advances), `rng` and
`ambientPeriodBars` (the periods, in `barsUntilChange`). Three branches of the Swift code no case reaches, as nothing
the SM's calls can do reaches them: the variation's `density < 0.4` (only the subtract lowers the density, and the
ending follows it), the payoff's re-activation of missing voices (the build has activated every voice before the
payoff) and the 0.3 floor (`0.85^6 > 0.3`: seven voices give at most six subtractions). The C test runs the `q` and
`z` bars one at a time, and checks every field of the C SM there against the bar before.

## The CC lanes and the scene morphs

`tests/equiv/cc.ref` is `RefTool cc` (`tools/reference/Sources/RefTool/Cc.swift`), for `app/engine/cc.c`. Integers
are decimal, a `Double` the 16 lower-case hex digits of its bit pattern; `#` lines are comments.

The lanes: `ModulationEngine.render(lanes: [lane], ccMap: ["filter": 74], voiceID: "v", in: lo..<hi, meter:
.fourFour, seed: 0)` of one sine lane (`render`'s lookup and sort are then the identity), in the windows
`lo..<min(lo + size, bars × 3840)`, lo = 0, size, 2 size, …, for the sizes 960, 37 and 3840. The lanes, in this
order: `bed` and `pad`, the templates' own (`Project.demo`'s bedPluck1: 16 bars, depth 0.3, center 0.6;
`Project.fullEnsemble`'s pad: 32 bars, 0.4, 0.5; phase 0), 10,000 bars each; then made-up ones (`CCLane(target:
"filter", shape: .sine, …)`): the two with a phase (0.25, 0.7), a phase of 0.999, depth 1 about 0.5 (0 and 127
reached), clipped at 1 (`clip-high`, `top`) and at 0 (`clip-low`, `bottom`), depth 0 (`flat`: tick 0 alone), cycles
of half a bar, 64 bars and 7.3 bars, over 100 to 2,000 bars. The lane's events in all its windows, one after the
other, are one stream; a window's events are the stream's with lo ≤ at < hi, so the stream is the whole of what the
windows return. It is written for the first size; a later size whose stream is the same is written `same` (it is,
for every lane: `render` is chunk-safe). RefTool checks that every event lies in its window.

| Line | Content |
| --- | --- |
| `lane <name> sine <cycleBars> <depth> <center> <phase> <controller> <bars>` | a lane (`Double`s as bits), rendered over `bars` bars |
| `w <size> <count>` | the lane's stream in windows of `size`: the `count` `c` lines that follow |
| `w <size> <count> same` | the stream in windows of `size` is the lane's first one |
| `c <at> <value>` | a `RenderedCC` (voice `v`, the lane line's controller) |

The scene morphs: `SceneMorph` and `sceneEmissions` are internal to LiveEngine, so a real `LiveEngine` runs them:
`Project.ambientDrift` (A natural minor) with its arranger off (`enabled = false`, so no phase fires a scene; its
voices have no lanes, so `renderCC` is `sceneEmissions` alone), rendered in windows of 37, 960, 2500 and 3840 ticks.
Between two windows, each request whose tick the render head has reached is enqueued at the head:
`.morphToScene(name:, bars:)`, `atTick:` the head; then `render` and `renderCC` of the window. Two sequences:
`ambient`, the template's scenes, 8 bars each: 10 requests (closed from 0.5 at tick 0, retargets mid-morph, two
binding to one bar line, the same scene twice), 40 morphs, 85 bars; `extra`, the template with a third scene `wide`
(drone 1.3, bedPluck1 -0.4, bedPluck2 1, pad 0: clamped, both ends), with 8, 1, 0 and 3 bars and morphs departing
from 0 and 1: 6 requests, 24 morphs, 30 bars.

| Line | Content |
| --- | --- |
| `voices <sequence> <id>…` | the project's voices, in order (all with `ccMap: ["filter": 74]`) |
| `mcase <sequence> <size> <bars>` | a fresh engine, windows of `size` over `bars` bars |
| `scene <head> <bind> <bars> <name> (<id>:<target>:<value>)…` | a request enqueued at `head`; `bind` is `barBindTick` (the next bar line of the head, `quantizedEventBar` × 3840; the head is `renderedUpTo`); the scene's destinations as defined (unclamped), sorted |
| `c <at> <id> <controller> <value>` | a `RenderedCC` of `renderCC`, in the order it returned them |

The morphs the C test makes from these lines (`startSceneMorph`, the live engine's): before a window lo..<hi, each
request with `bind < hi` not yet made, in file order (`render` binds every change before the window's end, and
`renderCC` then reads the morphs as they stand), gives for each destination the morph of key `id|target`: start =
`bl_morph_value` of the key's morph at `bind` (0.5 when there is none), end = `max(0, min(1, value))`, t0 = `bind`,
t1 = `bind + max(1, bars) × 3840`. The window's events are then `bl_morph_emit` of each key's morph in string order of
the keys (as `sceneMorphs.sorted(by: key)`), controller 74. So a window that crosses a bind tick loses the replaced
morph's events before that tick, in Swift as here.
