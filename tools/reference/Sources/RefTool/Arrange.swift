// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Jiří Píša
// RefTool arrange: the arrangement state machine (ArrangementSM: init, tickBar, advance; deriveBuildOrder) over the
// templates' voice lists and one made-up list of seven, for the four configs (the three form presets and the
// default, which is off), with the seed LiveEngine gives it, 400 bars each with ten fixed advances. The lines are in
// tests/equiv/README.md ("arrange.ref"); the port's arrange.c is checked against tests/equiv/arrange.ref.

import BerlinanEngine

func arrangeMode(_ args: [String]) throws -> Int32 {
    guard args.isEmpty else { throw ScriptError(description: "usage: RefTool arrange") }
    var out = Output()
    out.line("# arrange.ref: RefTool arrange (tests/equiv/README.md)")

    let presets: [(name: String, config: ArrangementConfig)] = [
        ("classic", .classicArc), ("slow", .slowBuild), ("ambient", .ambientPlateau), ("off", ArrangementConfig()),
    ]
    for p in presets {
        let c = p.config
        out.line("preset \(p.name) \(c.enabled ? 1 : 0) \(c.ambientMode ? 1 : 0) \(c.reprise == true ? 1 : 0)"
                 + " \(c.minPeriodBars) \(c.maxPeriodBars)")
    }

    // the voice lists: the templates' (the SM reads only ids and roles), then seven voices whose roles cover the
    // rank table's default (5 + offset: the aux at offset 0 still comes after the lead), a tie (two basses) and the
    // drone last
    var lists: [(name: String, voices: [VoiceConfig])] = Template.allCases.map { ($0.rawValue, $0.project.voices) }
    let base = Template.full.project.voices[0]
    let mix: [(String, VoiceRole)] = [("x", .aux), ("l", .lead), ("b1", .bass), ("r", .rhythm), ("b2", .bass),
                                      ("p2", .bedPluck2), ("d", .drone)]
    lists.append(("mix", mix.map { id, role in
        var v = base
        v.id = id
        v.role = role
        return v
    }))
    for list in lists {
        out.line("voices \(list.name)" + list.voices.map { " \($0.id) \($0.role.rawValue)" }.joined())
        out.line("order \(list.name) " + ArrangementSM.deriveBuildOrder(from: list.voices).joined(separator: " "))
    }

    struct Case {
        var list: Int, preset: Int, master: UInt64
        var adopt: Set<String>? = nil
        var locked: Set<String> = []
        var period = 22
        /// the locked set from a bar on (as LiveEngine reads the locks afresh at every boundary)
        var relocks: [(bar: Int, locked: Set<String>)] = []
    }
    var cases: [Case] = []
    let lockedSets: [Set<String>] = [[], ["bass"], ["bedPluck1", "pad"]]
    // the grid: three presets x the three templates x 30 seeds x three locked sets
    for preset in 0..<3 {
        for list in 0..<3 {
            for k in UInt64(0)..<30 {
                for locked in lockedSets { cases.append(Case(list: list, preset: preset, master: k, locked: locked)) }
            }
        }
    }
    // enabling mid-play: everything sounding, a part (bass and pad, of which a template may lack one), nothing
    for preset in 0..<3 {
        for list in 0..<3 {
            for k in UInt64(100)..<105 {
                for adopt in [Set(lists[list].voices.map(\.id)), ["bass", "pad"], []] {
                    cases.append(Case(list: list, preset: preset, master: k, adopt: adopt))
                }
            }
        }
    }
    // the ambient period: the floor (below 8, 8 itself) and above the default
    for list in 0..<3 {
        for k in UInt64(200)..<203 {
            for period in [-5, 0, 3, 7, 8, 9, 30] { cases.append(Case(list: list, preset: 2, master: k, period: period)) }
        }
    }
    // every voice locked (no target: the bed's and variation's empty events, the payoff's density alone); the
    // default config (off: tickBar does nothing, advance still works)
    for list in 0..<3 {
        for k in UInt64(300)..<305 {
            cases.append(Case(list: list, preset: 0, master: k, locked: Set(lists[list].voices.map(\.id))))
            cases.append(Case(list: list, preset: 3, master: k))
        }
    }
    // the locks changing during the piece: everything locked, then nothing (the round robin moved by no event
    // before); nothing, everything, the bass, nothing
    for preset in [0, 2] {
        for list in 0..<3 {
            for k in UInt64(500)..<505 {
                let all = Set(lists[list].voices.map(\.id))
                cases.append(Case(list: list, preset: preset, master: k, locked: all, relocks: [(60, []), (130, all)]))
                cases.append(Case(list: list, preset: preset, master: k,
                                  relocks: [(40, all), (70, ["bass"]), (100, []), (160, ["bedPluck1", "pad"])]))
            }
        }
    }
    // the list of seven: every preset, unlocked and with the two basses locked, adopted half way
    for preset in 0..<4 {
        for k in UInt64(400)..<410 {
            cases.append(Case(list: 3, preset: preset, master: k))
            cases.append(Case(list: 3, preset: preset, master: k, locked: ["b1", "b2"]))
            cases.append(Case(list: 3, preset: preset, master: k, adopt: ["d", "x", "b2", "l"]))
        }
    }

    // advance() before that bar's tickBar, as LiveEngine runs a boundary; twice at bar 120
    let advances = [5, 37, 38, 90, 120, 120, 200, 255, 310, 377]
    for c in cases {
        let voices = lists[c.list].voices
        let seed = SeedStream.derive(master: c.master, "arrangement")
        var sm = ArrangementSM(config: presets[c.preset].config, voices: voices, seed: seed,
                               adoptingActive: c.adopt, ambientPeriodBars: c.period)
        out.line("case \(lists[c.list].name) \(presets[c.preset].name) \(c.master) \(seed) \(c.period)"
                 + " \(setText(c.adopt, voices)) \(setText(c.locked, voices))")
        out.line("init " + stateText(sm))
        var locked = c.locked, quiet = 0, still = 0
        func flushRuns() {
            if quiet > 0 { out.line("q \(quiet)") }
            if still > 0 { out.line("z \(still)") }
            quiet = 0
            still = 0
        }
        for bar in 1...400 {
            for relock in c.relocks where relock.bar == bar {
                flushRuns()
                locked = relock.locked
                out.line("lock \(bar) \(setText(locked, voices))")
            }
            for _ in advances.filter({ $0 == bar }) {
                let d = sm.advance(locked: locked)
                flushRuns()
                out.line("a \(bar) " + stateText(sm) + directivesText(d))
            }
            let before = sm
            let d = sm.tickBar(locked: locked)
            // a run: no directives and the clock alone moved (one more bar in the phase, one less to the change),
            // or nothing moved (off, or stopped)
            let same = d.isEmpty && sm.phase == before.phase && sm.activeVoices == before.activeVoices
            if same && sm.barsInPhase == before.barsInPhase + 1 && sm.barsUntilChange == before.barsUntilChange - 1 {
                if still > 0 { flushRuns() }
                quiet += 1
            } else if same && sm.barsInPhase == before.barsInPhase && sm.barsUntilChange == before.barsUntilChange {
                if quiet > 0 { flushRuns() }
                still += 1
            } else {
                flushRuns()
                out.line("t \(bar) " + stateText(sm) + directivesText(d))
            }
        }
        flushRuns()
    }
    out.flush()
    return 0
}

/// A set of voice ids in project order, comma-joined: `-` for nil, `{}` for the empty set.
private func setText(_ set: Set<String>?, _ voices: [VoiceConfig]) -> String {
    guard let set else { return "-" }
    let ids = voices.map(\.id).filter(set.contains)
    return ids.isEmpty ? "{}" : ids.joined(separator: ",")
}

/// The SM's public fields: phase, barsInPhase, barsUntilChange, activeVoices (in their order, `-` when empty).
private func stateText(_ sm: ArrangementSM) -> String {
    "\(sm.phase.rawValue) \(sm.barsInPhase) \(sm.barsUntilChange) "
        + (sm.activeVoices.isEmpty ? "-" : sm.activeVoices.joined(separator: ","))
}

private func directivesText(_ ds: [ArrangementDirective]) -> String {
    ds.map { d in
        switch d {
        case .activate(let v): return " activate:\(v)"
        case .deactivate(let v): return " deactivate:\(v)"
        case .nudgeMutation(let v): return " nudge:\(v)"
        case .accentMove(let v): return " accent:\(v)"
        case .blankCycle(let v): return " blank:\(v)"
        case .setDensity(let x):
            let s = String(x.bitPattern, radix: 16)
            return " density:" + String(repeating: "0", count: 16 - s.count) + s
        case .fireBPart: return " bpart"
        case .thinToRoots(let v): return " thin:\(v)"
        case .requestStop: return " stop"
        }
    }.joined()
}
