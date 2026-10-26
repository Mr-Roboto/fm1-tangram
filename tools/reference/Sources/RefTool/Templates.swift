// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Jiří Píša
// RefTool templates: the three templates as the Mac instantiates them (TemplateStore.inDefaultKey: A natural minor,
// each with its own seed), every Project field the port reads, one line per item (tests/equiv/README.md). The port's
// templates.c is checked against tests/equiv/templates.ref.

import BerlinanEngine

func templatesMode(_ args: [String]) throws -> Int32 {
    guard args.isEmpty else { throw ScriptError(description: "usage: RefTool templates") }
    var out = Output()
    for template in Template.allCases {
        out.line("# template \(template.rawValue)")
        for line in templateLines(template.project) { out.line(line) }
    }
    out.flush()
    return 0
}

func templateLines(_ p: Project) -> [String] {
    func bits(_ d: Double) -> String { Dump.hex16(d.bitPattern) }
    func flag(_ b: Bool) -> String { b ? "1" : "0" }
    func opt<T>(_ v: T?, _ f: (T) -> String) -> String { v.map(f) ?? "-" }
    func set<T>(_ v: T?) -> String { v == nil ? "-" : "set" }
    var lines: [String] = []
    lines.append("seed \(p.masterSeed)")
    lines.append("bpm \(bits(p.bpm))")
    lines.append("key \(p.key.root)" + p.key.scale.offsets.map { " \($0)" }.joined())
    lines.append("meter \(p.meter.beatsPerBar) \(p.meter.beatUnit)" + p.meter.grouping.map { " \($0)" }.joined())
    for e in p.transposition.events { lines.append("event \(e.bar) \(e.degreeOffset)") }
    lines.append("arrangement " + opt(p.arrangement) { a in
        "\(flag(a.enabled)) \(a.minPeriodBars) \(a.maxPeriodBars) \(flag(a.ambientMode)) \(opt(a.reprise, flag)) "
            + opt(a.buildOrder) { $0.joined(separator: " ") }
    })
    lines.append("density " + opt(p.density, bits))
    lines.append("bpart " + opt(p.bPart) { b in
        "\(b.barsPerStep) \(flag(b.swapBass)) \(flag(b.dropBed))" + b.progressionSteps.map { " \($0)" }.joined()
    })
    lines.append("tension " + opt(p.tensionBudget) { "\($0)" })
    lines.append("legitimization " + opt(p.legitimization) { "\($0.repeats) \(opt($0.ratchetProbability, bits))" })
    lines.append("velocity " + opt(p.velocity) { "\($0.accentBoost) \($0.humanizeRange)" })
    lines.append("slides " + opt(p.slidesEnabled, flag))
    lines.append("unported \(set(p.harmony)) \(set(p.alternating)) \(set(p.snapshots))")
    for v in p.voices {
        lines.append("voice \(v.id) \(v.role.rawValue) \(v.register.low) \(v.register.high) \(v.referenceC)"
            + " \(v.channel) \(opt(v.phaseSteps) { "\($0)" }) \(opt(v.anchorPolicy) { $0.rawValue }) "
            + opt(v.participatesInTransposition, flag))
        lines.append("generator " + generatorText(v.generator))
        for l in v.ccLanes ?? [] {
            lines.append("lane \(l.target) \(l.shape.rawValue) \(bits(l.cycleBars)) \(bits(l.depth)) \(bits(l.center)) "
                + "\(bits(l.phase)) \(l.preStrikeTicks)")
        }
        for (target, controller) in (v.ccMap ?? [:]).sorted(by: { $0.key < $1.key }) {
            lines.append("ccmap \(target) \(controller)")
        }
        lines.append("unported \(set(v.gateRamp)) \(set(v.accumulator)) \(set(v.overridePattern)) \(set(v.midiDelay))")
    }
    for scene in p.ccScenes ?? [] {
        lines.append("scene \(scene.name)")
        for (voice, targets) in scene.values.sorted(by: { $0.key < $1.key }) {
            for (target, value) in targets.sorted(by: { $0.key < $1.key }) {
                lines.append("value \(voice) \(target) \(bits(value))")
            }
        }
    }
    for (phase, scene) in (p.sceneByPhase ?? [:]).sorted(by: { $0.key < $1.key }) {
        lines.append("phase \(phase) \(scene)")
    }
    return lines
}

private func generatorText(_ g: GeneratorConfig) -> String {
    switch g {
    case .seedProcedure(let bars, let mutations): "seedProcedure \(bars) \(mutations)"
    case .highMidLow(let register): "highMidLow \(register.rawValue)"
    case .pulsingBass(let accents): "pulsingBass \(accents)"
    case .drunkardsWalk(let bars, let scatter): "drunkardsWalk \(bars) \(scatter)"
    case .stagedPluck(let bars): "stagedPluck \(bars)"
    case .arpeggiator(let degrees, let octaves, let direction, let rate):
        "arpeggiator \(octaves) \(direction.rawValue) \(rate.rawValue)" + degrees.map { " \($0)" }.joined()
    case .bassMelody(let bars): "bassMelody \(bars)"
    case .chordLayer(let periodBars): "chordLayer \(periodBars)"
    case .heartbeat: "heartbeat"
    case .drone(let bars): "drone \(bars)"
    }
}
