// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Jiří Píša
// RefTool gen: the patterns of the generators the templates use (PatternFactory.make, makeBassMelody), in A minor
// and C minor, 200 seeds each, and on every 104th of them the pattern ops (PatternOps.densityCap, thinToRoots,
// moveAccents) and the loop cursor (withLength, rotated, occurrences, hasOnset). The lines are in
// tests/equiv/README.md ("gen.ref"); the port's pattern.c and gen.c are checked against tests/equiv/gen.ref.

import BerlinanEngine

func genMode(_ args: [String]) throws -> Int32 {
    guard args.isEmpty else { throw ScriptError(description: "usage: RefTool gen") }
    let keys: [Key] = [Key(root: 9, scale: .preset(.naturalMinor)), Key.default]
    let seeds: [UInt64] = (0..<200).map { SeedStream.derive(master: UInt64($0), "gen") }
    // the generator configs of the three templates, each with the role it plays there (the seed procedure also as
    // the bass, its other branch)
    let configs: [(GeneratorConfig, VoiceRole)] = [
        (.seedProcedure(bars: 1, mutations: 4), .bedPluck1), (.seedProcedure(bars: 1, mutations: 4), .bedPluck2),
        (.seedProcedure(bars: 1, mutations: 4), .bass), (.seedProcedure(bars: 1, mutations: 3), .bedPluck1),
        (.seedProcedure(bars: 1, mutations: 3), .bedPluck2), (.seedProcedure(bars: 1, mutations: 3), .bass),
        (.pulsingBass(accents: 3), .bass), (.stagedPluck(bars: 2), .lead), (.bassMelody(bars: 16), .bass),
        (.chordLayer(periodBars: 7), .pad), (.heartbeat, .rhythm), (.drone(bars: 8), .drone),
    ]
    // the bass melody's walk over the full ensemble's resolved schedule (the demo's transposition), and over a busy
    // one with a change every bar, steps up to 14 degrees (more than three passing tones can walk)
    let demo = HarmonyScheduler.resolve(project: .fullEnsemble).schedule
    let busy = TranspositionSchedule(events: [0, 7, -7, 2, 5, -3, 0, 0, 4, -6, 1, 3, -2, 6, 0, -5].enumerated().map {
        TranspositionEvent(bar: $0.offset, degreeOffset: $0.element)
    })
    let densities: [Double] = [0, 0.01, 0.35, 0.5, 0.99, 1, 1.5, -0.5]
    var out = Output()
    out.line("# gen.ref: RefTool gen (tests/equiv/README.md)")
    var count = 0
    func ops(_ p: Pattern) {
        defer { count += 1 }
        guard count % 104 == 0 else { return }
        for d in densities {
            let q = PatternOps.densityCap(p, density: d, meter: .fourFour)
            out.line("cap \(Dump.hex16(d.bitPattern)) \(q.length) " + notesText(q))
        }
        let r = PatternOps.thinToRoots(p)
        out.line("roots \(r.length) \(r.locked ? 1 : 0) \(remainingText(r.lifecycle)) " + notesText(r))
        let seed = SeedStream.derive(master: UInt64(count), "accents")
        out.line("accents \(seed) " + notesText(PatternOps.moveAccents(p, seed: seed)))
        for (delta, rotate, lo) in [(Tick(-240), Tick(0), Tick(1234)), (480, -1000, -3000)] {
            let q = p.withLength(p.length + delta).rotated(by: rotate)
            let hi = lo + 2 * q.length + 777
            let occ = q.occurrences(in: lo..<hi)
            out.line("occ \(q.length) \(rotate) \(lo) \(hi) \(q.startOffset) \(occ.count)"
                     + occ.map { o in " \(o.at) \(q.notes.firstIndex(of: o.note)!)" }.joined())
            // every 20 ticks from a multiple of 20 (the positions, the rotations and the lengths all are)
            let step: Tick = 20, from = lo - ((lo % step) + step) % step
            let onsets = stride(from: from, to: hi, by: Int(step)).map { q.hasOnset(atGlobalTick: $0) ? "1" : "0" }
            out.line("onset \(q.length) \(rotate) \(from) \(step) " + onsets.joined())
        }
    }
    // and two that no template uses, in A minor only: more mutations, so the degree moves reach the clamp at 4
    let extra: [(GeneratorConfig, VoiceRole)] = [
        (.seedProcedure(bars: 1, mutations: 16), .bedPluck1), (.seedProcedure(bars: 2, mutations: 8), .bedPluck1),
    ]
    for (config, role, keys) in configs.map({ ($0.0, $0.1, keys) }) + extra.map({ ($0.0, $0.1, [keys[0]]) }) {
        for key in keys {
            for seed in seeds {
                let p = PatternFactory.make(id: "gen", config: config, role: role, key: key, meter: .fourFour,
                                            seed: seed)
                out.line("gen \(configText(config)) \(role.rawValue) \(key.root) \(seed) \(p.length) "
                         + "\(p.startOffset) \(p.provenance.generator.rawValue) \(remainingText(p.lifecycle)) "
                         + notesText(p))
                ops(p)
            }
        }
    }
    for schedule in [demo, busy] {
        for seed in seeds {
            let p = PatternFactory.makeBassMelody(id: "walk", bars: 16, schedule: schedule, meter: .fourFour,
                                                  seed: seed)
            out.line("walk 16 \(schedule.events.count)"
                     + schedule.events.map { " \($0.bar) \($0.degreeOffset)" }.joined()
                     + " \(seed) \(p.length) " + notesText(p))
            ops(p)
        }
    }
    out.flush()
    return 0
}

/// `<n>` then per note `<position> <gate> <degree> <octave> <accidental> <velocity> <accent> <ratchet>`
func notesText(_ p: Pattern) -> String {
    "\(p.notes.count)" + p.notes.map { n in
        " \(n.position) \(n.gate) \(n.pitch.degree) \(n.pitch.octave) \(n.pitch.accidental) \(n.velocity)"
            + " \(n.accent ? 1 : 0) \(n.ratchet)"
    }.joined()
}

func remainingText(_ l: Lifecycle) -> String {
    switch l {
    case .repeating(let remaining): "\(remaining)"
    case .established: "E"
    }
}

/// `<kind> <a> <b>`, the two parameters 0 where the config has fewer
func configText(_ g: GeneratorConfig) -> String {
    switch g {
    case .seedProcedure(let bars, let mutations): "seedProcedure \(bars) \(mutations)"
    case .pulsingBass(let accents): "pulsingBass \(accents) 0"
    case .stagedPluck(let bars): "stagedPluck \(bars) 0"
    case .bassMelody(let bars): "bassMelody \(bars) 0"
    case .chordLayer(let periodBars): "chordLayer \(periodBars) 0"
    case .heartbeat: "heartbeat 0 0"
    case .drone(let bars): "drone \(bars) 0"
    default: "unported 0 0"
    }
}
