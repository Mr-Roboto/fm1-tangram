// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Jiří Píša
// RefTool harmony: the transposition schedule (TranspositionSchedule(events:), offset(atBar:)) over 100 random event
// lists, HarmonyEngine.quantizedEventBar over 50 ticks, VelocityPolicy().apply over 300 inputs, and the note pipeline
// as LiveEngine runs it for one voice (HarmonyEngine.render, then the velocity policy at the loop position) on the
// templates' voices over 64 bars in windows of 37, 960 and 3840 ticks. The lines are in tests/equiv/README.md
// ("harmony.ref"); the port's harmony.c is checked against tests/equiv/harmony.ref.

import BerlinanEngine

func harmonyMode(_ args: [String]) throws -> Int32 {
    guard args.isEmpty else { throw ScriptError(description: "usage: RefTool harmony") }
    var out = Output()
    out.line("# harmony.ref: RefTool harmony (tests/equiv/README.md)")
    var g = SplitMix64(seed: 0xBE12_71A9)

    // the schedule: lists of 0...40 events in input order, the bars drawn from 0...8, 0...40 or 0...200 (the
    // narrower, the more bars come twice or more: the last one wins)
    for i in 0..<100 {
        let span = [8, 40, 200][i % 3]
        let events = (0..<(i % 41)).map { _ in
            let bar = Int.random(in: 0...span, using: &g)
            return TranspositionEvent(bar: bar, degreeOffset: Int.random(in: -7...7, using: &g))
        }
        let s = TranspositionSchedule(events: events)
        out.line("sched \(events.count)" + events.map { " \($0.bar) \($0.degreeOffset)" }.joined()
                 + " \(s.events.count)" + s.events.map { " \($0.bar) \($0.degreeOffset)" }.joined()
                 + (0...200).map { " \(s.offset(atBar: $0))" }.joined())
    }

    // the quantized bar: on and around the bar lines, then anywhere below 2^40
    var ticks: [Tick] = [0, 1, 2, 959, 960, 3839, 3840, 3841, 7679, 7680, 7681, 11519, 11520, 38400, 38401]
    while ticks.count < 50 { ticks.append(Tick.random(in: 0..<(1 << 40), using: &g)) }
    for t in ticks {
        out.line("bar \(t) \(HarmonyEngine.quantizedEventBar(requestTick: t, meter: .fourFour))")
    }

    // the velocity policy (the default: accent 20, humanize 10): every velocity a UInt8 holds, so both clamps are
    // reached, positions in and out of a loop, a few negative
    let policy = VelocityPolicy()
    for i in 0..<300 {
        let v = UInt8(i < 256 ? i : Int.random(in: 0...255, using: &g))
        let accent = Bool.random(using: &g)
        let seed = g.next()
        let local = i % 10 == 9 ? -Tick.random(in: 1...5000, using: &g) : Tick.random(in: 0..<16 * 3840, using: &g)
        let r = policy.apply(velocity: v, accent: accent, patternSeed: seed, localPosition: local)
        out.line("vel \(v) \(accent ? 1 : 0) \(seed) \(local) \(r)")
    }

    // the pipeline: each template's voices as LiveEngine(project:) materializes them (gen0, its own seed, A natural
    // minor) against the template's schedule; then the full ensemble's against a busy one (a change every bar of
    // -7...7 degrees) with the lead left out of the transposition
    let busy = TranspositionSchedule(events: (0..<64).map {
        TranspositionEvent(bar: $0, degreeOffset: Int.random(in: -7...7, using: &g))
    })
    typealias Pass = (name: String, project: Project, schedule: TranspositionSchedule, leadOut: Bool)
    var passes: [Pass] = Template.allCases.map {
        ($0.rawValue, $0.project, HarmonyScheduler.resolve(project: $0.project).schedule, false)
    }
    passes.append(("busy", Template.full.project, busy, true))
    let bars = 64, total = Tick(bars) * Meter.fourFour.ticksPerBar
    for pass in passes {
        let p = pass.project
        let engine = LiveEngine(project: p)
        let velocity = p.velocity ?? VelocityPolicy()
        out.line("schedule \(pass.name) \(pass.schedule.events.count)"
                 + pass.schedule.events.map { " \($0.bar) \($0.degreeOffset)" }.joined())
        out.line("key \(p.key.root) \(p.key.scale.mode.rawValue)")
        for (index, config) in p.voices.enumerated() {
            var voice = engine.voiceState(id: config.id)!.voice
            if pass.leadOut && voice.role == .lead { voice.participatesInTransposition = false }
            let pat = voice.pattern
            out.line("voice \(index) \(voice.id) \(voice.register.low) \(voice.register.high) \(voice.referenceC)"
                     + " \(voice.participatesInTransposition ? 1 : 0) \(pat.provenance.seed) \(pat.length)"
                     + " \(pat.startOffset) " + notesText(pat))
            // LiveEngine.renderVoice with the density at 1: the ties (none here), the render, the velocity at the
            // note's loop position with the pattern's own seed
            var source = voice
            source.pattern = PatternOps.applyTies(source.pattern)
            for window in [Tick(37), 960, 3840] {
                out.line("window \(window)")
                var lo: Tick = 0
                while lo < total {
                    let hi = min(lo + window, total)
                    let notes = HarmonyEngine.render(voices: [source], schedule: pass.schedule, key: p.key,
                                                     meter: p.meter, in: lo..<hi)
                    if !notes.isEmpty {
                        out.line("w \(lo) \(notes.count)" + notes.map { n in
                            let local = Tick(floorMod(Int(n.at + pat.startOffset), Int(pat.length)))
                            let v = velocity.apply(velocity: n.velocity, accent: n.accent,
                                                   patternSeed: pat.provenance.seed, localPosition: local)
                            return " \(n.at) \(n.midiNote) \(n.velocity) \(v) \(n.gate) \(n.accent ? 1 : 0)"
                                + " \(n.ratchet)"
                        }.joined())
                    }
                    lo = hi
                }
            }
        }
    }
    out.flush()
    return 0
}

/// The engine's floorMod (DegreePitch.swift), internal there: the same body.
private func floorMod(_ a: Int, _ b: Int) -> Int {
    let r = a % b
    return (r != 0 && (r < 0) != (b < 0)) ? r + b : r
}
