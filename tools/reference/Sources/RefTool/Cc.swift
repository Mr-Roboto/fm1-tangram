// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Jiří Píša
// RefTool cc: the CC lanes (ModulationEngine.render of one sine lane: the templates' two over 10,000 bars and 13 more
// with a phase, clipping depths and centres, short and long cycles) in windows of 960, 37 and 3840 ticks, and the
// scene morphs (LiveEngine's sceneEmissions, reached through a real LiveEngine on the ambient template with the
// arranger off: .morphToScene enqueued at chosen ticks, renderCC over every window) in windows of 37, 960, 2500 and
// 3840 ticks. The lines are in tests/equiv/README.md ("cc.ref"); the port's cc.c is checked against
// tests/equiv/cc.ref.

import BerlinanEngine

func ccMode(_ args: [String]) throws -> Int32 {
    guard args.isEmpty else { throw ScriptError(description: "usage: RefTool cc") }
    var out = Output()
    out.line("# cc.ref: RefTool cc (tests/equiv/README.md)")
    func bits(_ d: Double) -> String { Dump.hex16(d.bitPattern) }
    let barTicks = Meter.fourFour.ticksPerBar

    // ---- the lanes: the templates' own (demo's bedPluck1, the full ensemble's pad), then made-up ones
    let bed = Project.demo.voices.first { $0.id == "bedPluck1" }!.ccLanes![0]
    let pad = Project.fullEnsemble.voices.first { $0.id == "pad" }!.ccLanes![0]
    func sine(_ cycle: Double, _ depth: Double, _ center: Double, _ phase: Double) -> CCLane {
        CCLane(target: "filter", shape: .sine, cycleBars: cycle, depth: depth, center: center, phase: phase)
    }
    let lanes: [(name: String, lane: CCLane, bars: Int)] = [
        ("bed", bed, 10_000),
        ("pad", pad, 10_000),
        ("bed-phase", sine(16, 0.3, 0.6, 0.25), 1000),
        ("pad-phase", sine(32, 0.4, 0.5, 0.7), 1000),
        ("late-phase", sine(8, 0.5, 0.5, 0.999), 500),
        ("full", sine(4, 1, 0.5, 0), 150),                  // 0 and 127 exactly
        ("clip-high", sine(8, 1, 0.9, 0.1), 500),           // clipped at 1
        ("clip-low", sine(6, 0.8, 0.1, 0.33), 500),         // clipped at 0
        ("flat", sine(16, 0, 0.37, 0), 100),                // one value: tick 0 alone
        ("half-bar", sine(0.5, 1, 0.5, 0), 100),
        ("half-bar-phase", sine(0.5, 0.3, 0.6, 0.5), 200),
        ("long", sine(64, 1, 0.5, 0.6180339887), 2000),
        ("odd", sine(7.3, 0.65, 0.45, 0.123), 400),
        ("top", sine(3, 1, 1, 0), 150),                     // half the cycle at 1
        ("bottom", sine(3, 1, 0, 0.5), 150),                // half the cycle at 0
    ]
    let ccMap: [String: UInt8] = ["filter": 74]
    for (name, lane, bars) in lanes {
        guard lane.shape == .sine else { throw ScriptError(description: "lane \(name): sine only") }
        out.line("lane \(name) \(lane.shape.rawValue) \(bits(lane.cycleBars)) \(bits(lane.depth)) \(bits(lane.center))"
                 + " \(bits(lane.phase)) 74 \(bars)")
        let total = Tick(bars) * barTicks
        var first: [RenderedCC]? = nil
        for size: Tick in [960, 37, 3840] {
            var stream: [RenderedCC] = []
            var lo: Tick = 0
            while lo < total {
                let hi = min(lo + size, total)
                let window = ModulationEngine.render(lanes: [lane], ccMap: ccMap, voiceID: "v", in: lo..<hi,
                                                     meter: .fourFour, seed: 0)
                for cc in window where cc.at < lo || cc.at >= hi || cc.voiceID != "v" || cc.controller != 74 {
                    throw ScriptError(description: "lane \(name): \(cc) out of the window \(lo)..<\(hi)")
                }
                stream += window
                lo = hi
            }
            if let first, stream == first {
                out.line("w \(size) \(stream.count) same")
            } else {
                out.line("w \(size) \(stream.count)")
                for cc in stream { out.line("c \(cc.at) \(cc.value)") }
            }
            if first == nil { first = stream }
        }
    }

    // ---- the scene morphs: the ambient template (A natural minor) with the arranger off, so that only the
    // enqueued .morphToScene actions start morphs; its voices have no lanes, so renderCC is sceneEmissions alone
    var project = Template.ambient.project
    var config = project.arrangement ?? ArrangementConfig()
    config.enabled = false
    project.arrangement = config
    // one more scene for the extra sequence: destinations beyond 0...1 (startSceneMorph clamps them) and at both ends
    var extra = project
    extra.ccScenes!.append(CCScene(name: "wide", values: [
        "drone": ["filter": 1.3], "bedPluck1": ["filter": -0.4], "bedPluck2": ["filter": 1.0], "pad": ["filter": 0.0],
    ]))
    // (request tick, scene, bars); each sequence's requests in tick order
    let sequences: [(name: String, project: Project, bars: Int, requests: [(tick: Tick, scene: String, bars: Int)])] = [
        // the brief's: the ambient scenes, 8 bars, from 0.5; retargets mid-morph, two binding to one bar line
        ("ambient", project, 85, [
            (0, "closed", 8), (5 * 3840 + 100, "open", 8), (20 * 3840, "closed", 8), (25 * 3840, "open", 8),
            (25 * 3840 + 1, "closed", 8), (33 * 3840 + 1234, "open", 8), (50 * 3840, "closed", 8),
            (52 * 3840 + 7, "open", 8), (52 * 3840 + 8, "open", 8), (70 * 3840, "closed", 8),
        ]),
        // the extra: the clamped destinations, 1, 0 (max(1, bars)) and 3 bars, and morphs departing from 0 and 1
        // (the wide scene settled on the pad and the drone)
        ("extra", extra, 30, [
            (0, "wide", 8), (3 * 3840, "open", 1), (3 * 3840 + 5, "closed", 0), (10 * 3840, "wide", 3),
            (11 * 3840 + 999, "wide", 1), (17 * 3840, "closed", 8),
        ]),
    ]
    for seq in sequences {
        out.line("voices \(seq.name)" + seq.project.voices.map { " \($0.id)" }.joined())
        let total = Tick(seq.bars) * barTicks
        for size: Tick in [37, 960, 2500, 3840] {
            out.line("mcase \(seq.name) \(size) \(seq.bars)")
            var engine = LiveEngine(project: seq.project)
            var next = 0
            var head: Tick = 0
            while head < total {
                // the requests due at this head, enqueued between two windows (renderedUpTo is the head)
                while next < seq.requests.count && seq.requests[next].tick <= head {
                    let r = seq.requests[next]
                    let scene = seq.project.ccScenes!.first { $0.name == r.scene }!
                    // LiveEngine.barBindTick: the next bar line of max(request, renderedUpTo)
                    let bind = Tick(HarmonyEngine.quantizedEventBar(requestTick: head, meter: .fourFour)) * barTicks
                    out.line("scene \(head) \(bind) \(r.bars) \(r.scene)"
                             + scene.values.keys.sorted().map { id in
                                 scene.values[id]!.keys.sorted().map { " \(id):\($0):\(bits(scene.values[id]![$0]!))" }
                                     .joined()
                             }.joined())
                    engine.enqueue(.morphToScene(name: r.scene, bars: r.bars), atTick: head)
                    next += 1
                }
                let end = min(head + size, total)
                _ = engine.render(range: head..<end)
                for cc in engine.renderCC(range: head..<end) {
                    guard cc.at >= head && cc.at < end else {
                        throw ScriptError(description: "\(seq.name): \(cc) out of the window \(head)..<\(end)")
                    }
                    out.line("c \(cc.at) \(cc.voiceID) \(cc.controller) \(cc.value)")
                }
                head = end
            }
        }
    }
    out.flush()
    return 0
}
