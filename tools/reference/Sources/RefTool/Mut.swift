// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Jiří Píša
// RefTool mut: legitimization and mutation (Legitimizer.advanceAtLoopBoundary, isMutable, mutate) on 300 voices of
// the templates' generators, each mutated ten times in a row as LiveEngine does it (the lifecycle counted down to
// .established between two mutations), with the identity cases (still repeating, locked) among them; then 20
// lifecycle sequences. The lines are in tests/equiv/README.md ("mut.ref"); the port's legit.c is checked against
// tests/equiv/mut.ref.

import BerlinanEngine

func mutMode(_ args: [String]) throws -> Int32 {
    guard args.isEmpty else { throw ScriptError(description: "usage: RefTool mut") }
    // the voices as the templates configure them (id, role, register, reference C), the bass with the demo's anchor
    // rule; a phase of -240 is bedPluck2's phaseSteps -1 (a loop shorter than a bar), here also on two more
    typealias Case = (id: String, role: VoiceRole, config: GeneratorConfig, phase: Tick, anchor: AnchorPolicy,
                      low: UInt8, high: UInt8, refC: Int)
    let cases: [Case] = [
        ("bedPluck1", .bedPluck1, .seedProcedure(bars: 1, mutations: 4), 0, .none, 55, 79, 60),
        ("bedPluck2", .bedPluck2, .seedProcedure(bars: 1, mutations: 4), -240, .none, 60, 84, 60),
        ("bass", .bass, .pulsingBass(accents: 3), 0, .beats1and3, 24, 48, 36),
        ("bass", .bass, .bassMelody(bars: 16), 0, .beats1and3, 24, 48, 36),
        ("pad", .pad, .chordLayer(periodBars: 7), 0, .none, 48, 72, 60),
        ("kick", .rhythm, .heartbeat, 0, .none, 36, 47, 36),
        ("drone", .drone, .drone(bars: 8), 0, .none, 36, 60, 48),
        ("lead", .lead, .stagedPluck(bars: 2), 0, .none, 60, 96, 72),
        ("bedPluck1", .bedPluck1, .seedProcedure(bars: 1, mutations: 3), 0, .beat1, 55, 79, 60),
        ("bedPluck2", .bedPluck2, .seedProcedure(bars: 1, mutations: 3), -240, .beat1, 60, 84, 60),
        ("pad", .pad, .chordLayer(periodBars: 7), 0, .beat1, 48, 72, 60),
        ("bass", .bass, .pulsingBass(accents: 3), -240, .beats1and3, 24, 48, 36),
    ]
    // 25 voices a case: 10 in A natural minor, 10 in C natural minor (Key.default), 5 in A minor pentatonic (a scale
    // of 5, so the degree shifts draw from -5...5)
    let keys: [(Key, Int)] = [(Key(root: 9, scale: .preset(.naturalMinor)), 10), (Key.default, 10),
                              (Key(root: 9, scale: .preset(.minorPentatonic)), 5)]
    let ratchets: [Double?] = [nil, 0.5, nil, 0, nil, 0.5]
    let chain = 10
    var out = Output()
    out.line("# mut.ref: RefTool mut (tests/equiv/README.md)")
    func advance(_ v: inout Voice, _ count: Int) {
        var text = "advance \(count)"
        for _ in 0..<count {
            v.pattern = Legitimizer.advanceAtLoopBoundary(v.pattern)
            text += " \(remainingText(v.pattern.lifecycle)) \(Legitimizer.isMutable(v.pattern) ? 1 : 0)"
        }
        out.line(text)
    }
    func mutate(_ v: inout Voice, key: Key, master: UInt64, gen: Int, repeats: Int, ratchet: Double?) {
        let seed = SeedStream.derive(master: master, "voice", v.id, "mut", String(gen))
        v = Legitimizer.mutate(v, key: key, meter: .fourFour, seed: seed,
                               config: Legitimizer.Config(repeats: repeats, ratchetProbability: ratchet))
        let p = v.pattern
        out.line("mut \(gen) \(seed) \(repeats) \(ratchet.map { Dump.hex16($0.bitPattern) } ?? "nil")"
                 + " \(p.locked ? 1 : 0) \(remainingText(p.lifecycle)) \(p.length) \(p.startOffset) " + notesText(p))
    }
    var i = 0
    for c in cases {
        var k = 0
        for (key, count) in keys {
            for _ in 0..<count {
                defer { i += 1; k += 1 }
                let master = SeedStream.derive(master: UInt64(k), "gen")
                var pattern = PatternFactory.make(id: c.id, config: c.config, role: c.role, key: key,
                                                  meter: .fourFour, seed: master)
                if c.phase != 0 {
                    pattern = PhasingOps.phasedCopy(of: pattern, newLength: pattern.length + c.phase,
                                                    idSuffix: "phased")
                }
                var v = Voice(id: c.id, role: c.role, register: RegisterWindow(low: c.low, high: c.high),
                              referenceC: c.refC, pattern: pattern, anchorPolicy: c.anchor)
                let repeats = i % 2 == 0 ? 4 : 16, ratchet = ratchets[i % ratchets.count]
                let p = v.pattern
                out.line("voice \(v.id) \(v.role.rawValue) \(v.anchorPolicy.rawValue) \(v.register.low)"
                         + " \(v.register.high) \(v.referenceC) \(v.participatesInTransposition ? 1 : 0)"
                         + " \(key.root) \(key.scale.mode.rawValue) \(configText(c.config)) \(master) \(c.phase)"
                         + " \(p.length) \(p.startOffset) \(p.locked ? 1 : 0) \(remainingText(p.lifecycle)) "
                         + notesText(p))
                var gen = 0
                // the identity: fresh material is still repeating
                if i % 10 == 0 { mutate(&v, key: key, master: master, gen: gen, repeats: repeats, ratchet: ratchet) }
                advance(&v, 4)
                // the identity: established but locked
                if i % 10 == 5 {
                    v.pattern.locked = true
                    out.line("lock 1")
                    mutate(&v, key: key, master: master, gen: gen, repeats: repeats, ratchet: ratchet)
                    v.pattern.locked = false
                    out.line("lock 0")
                }
                while gen < chain {
                    mutate(&v, key: key, master: master, gen: gen, repeats: repeats, ratchet: ratchet)
                    gen += 1
                    if gen < chain { advance(&v, repeats) }
                }
            }
        }
    }
    // the lifecycle alone: from each start, locked and not, two boundaries past .established
    for start in [-1, 0, 1, 2, 3, 4, 5, 8, 16, 32] {
        for locked in [false, true] {
            var v = Voice(id: "life", role: .bedPluck1, register: RegisterWindow(low: 55, high: 79), referenceC: 60,
                          pattern: PatternFactory.make(id: "life", config: .heartbeat, role: .rhythm,
                                                       key: Key.default, meter: .fourFour, seed: 1))
            v.pattern.locked = locked
            v.pattern.lifecycle = start < 0 ? .established : .repeating(remaining: start)
            out.line("life \(remainingText(v.pattern.lifecycle)) \(locked ? 1 : 0)"
                     + " \(Legitimizer.isMutable(v.pattern) ? 1 : 0)")
            advance(&v, max(start, 0) + 2)
        }
    }
    out.flush()
    return 0
}
