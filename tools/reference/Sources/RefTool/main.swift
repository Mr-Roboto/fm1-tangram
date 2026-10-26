// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Jiří Píša
// RefTool: the reference writer over Berlinan's pinned engine. One function per mode, listed in `modes`; a later task
// adds its mode (rng, gen, mut, harmony, arrange, cc, midi) as one more function and one more line there.
//   RefTool script [--text] FILE   the reference lines of a script (tests/equiv/README.md); --text writes the state
//                                  text in place of each H line, for finding a mismatch
//   RefTool templates              the three templates as the Mac instantiates them
//   RefTool rng                    SplitMix64, seed streams and the standard library's random draws, for rng.c
//   RefTool selftest               the tool checked against the engine's own offline renderer

import BerlinanEngine
import Foundation

let modes: [(name: String, usage: String, run: ([String]) throws -> Int32)] = [
    ("script", "script [--text] FILE", scriptMode),
    ("templates", "templates", templatesMode),
    ("rng", "rng", rngMode),
    ("selftest", "selftest", selftestMode),
]

func scriptMode(_ args: [String]) throws -> Int32 {
    let showText = args.first == "--text"
    let files = showText ? Array(args.dropFirst()) : args
    guard files.count == 1 else { throw ScriptError(description: "usage: RefTool script [--text] FILE") }
    let text = try String(contentsOfFile: files[0], encoding: .utf8)
    var run = Run(try Script(text: text))
    run.showText = showText
    var out = Output()
    run.play(emit: { out.line($0) })
    out.flush()
    return 0
}

/// The random draws of the engine, through the standard library's own functions on a SplitMix64: the lines are in
/// tests/equiv/README.md ("rng.ref"). A generator is a fresh SplitMix64 per line.
func rngMode(_ args: [String]) throws -> Int32 {
    let seeds: [UInt64] = [
        0, 1, 0xBE12_71A9, 2_477_861_893, 0x0A3B_11ED, 0x0123_4567_89AB_CDEF, 0xFFFF_FFFF_FFFF_FFFF, 0x8000_0000_0000_0000,
        0x0000_0000_FFFF_FFFF, 0x1_0000_0000, 2, 3, 7, 42, 12345, 0xDEAD_BEEF, 0xCAFE_BABE_F00D_D00D, 0x5555_5555_5555_5555,
        0xAAAA_AAAA_AAAA_AAAA, 0x9E37_79B9_7F4A_7C15, 0x1234, 0xFFFF_FFFF, 1_000_000_007, 0x7FFF_FFFF_FFFF_FFFF,
    ]
    var out = Output()
    out.line("# rng.ref: RefTool rng (tests/equiv/README.md)")
    for seed in seeds {
        var g = SplitMix64(seed: seed)
        out.line("next \(seed) " + (0..<64).map { _ in String(g.next()) }.joined(separator: " "))
    }
    let lists: [[String]] = [
        ["voice", "bass", "mut", "12"], ["humanize", "-240"], ["arrangement"], ["a"], ["ab", "c"], ["a", "bc"],
        ["voice", "pad", "gen"], ["voice", "lead", "mut", "0"], ["voice", "kick", "mut", "7"], ["scene", "open"],
        ["transposition"], ["x", "y", "z"], ["voice", "bedPluck1", "gen", "3"], ["density"], ["voice", "drone"],
        ["humanize", "240", "velocity"],
    ]
    for master in [UInt64(0xBE12_71A9), 0, 0xFFFF_FFFF_FFFF_FFFF] {
        for labels in lists {
            out.line("derive \(master) \(SeedStream.derive(master: master, labels: labels)) " + labels.joined(separator: " "))
        }
    }
    let closed: [ClosedRange<Int>] = [1...12, 0...3, -7...7, 2...4, 102...138]
    let open: [Range<Int>] = [0..<16, 0..<1]
    var k = 0
    for r in closed {
        var g = SplitMix64(seed: seeds[2 + k % 6]); k += 1
        out.line("in \(seeds[2 + (k - 1) % 6]) \(r.lowerBound) \(r.upperBound) "
                 + (0..<2000).map { _ in String(Int.random(in: r, using: &g)) }.joined(separator: " "))
    }
    for r in open {
        var g = SplitMix64(seed: seeds[2 + k % 6]); k += 1
        out.line("upto \(seeds[2 + (k - 1) % 6]) \(r.lowerBound) \(r.upperBound) "
                 + (0..<2000).map { _ in String(Int.random(in: r, using: &g)) }.joined(separator: " "))
    }
    // huge ranges, where Lemire's rejection is common (the bound near or above 2^63)
    let hugeClosed: [ClosedRange<Int>] = [0...(Int.max - 3), -(1 << 62)...(1 << 62), Int.min / 2...Int.max / 2,
                                          Int.min / 2...Int.max]
    let hugeOpen: [Range<Int>] = [0..<(3 << 61), Int.min / 2..<Int.max]
    for seed in [seeds[2], seeds[1], seeds[6]] {
        for r in hugeClosed {
            var g = SplitMix64(seed: seed)
            out.line("in \(seed) \(r.lowerBound) \(r.upperBound) "
                     + (0..<2000).map { _ in String(Int.random(in: r, using: &g)) }.joined(separator: " "))
        }
        for r in hugeOpen {
            var g = SplitMix64(seed: seed)
            out.line("upto \(seed) \(r.lowerBound) \(r.upperBound) "
                     + (0..<2000).map { _ in String(Int.random(in: r, using: &g)) }.joined(separator: " "))
        }
    }
    for seed in [seeds[2], seeds[0], seeds[6]] {
        var g = SplitMix64(seed: seed)
        out.line("bool \(seed) " + (0..<2000).map { _ in Bool.random(using: &g) ? "1" : "0" }.joined(separator: " "))
    }
    for seed in [seeds[2], seeds[1], seeds[5]] {
        var g = SplitMix64(seed: seed)
        out.line("unit \(seed) " + (0..<2000).map { _ in
            let s = String(Double.random(in: 0..<1, using: &g).bitPattern, radix: 16)
            return String(repeating: "0", count: 16 - s.count) + s
        }.joined(separator: " "))
    }
    for count in 1...17 {
        var g = SplitMix64(seed: seeds[2])
        let items = Array(0..<count)
        out.line("index \(seeds[2]) \(count) " + (0..<500).map { _ in String(items.randomElement(using: &g)!) }.joined(separator: " "))
    }
    out.flush()
    return 0
}

/// Two checks. Parity: for the full ensemble with the classic arc, the script's notes in windows of one bar equal
/// OfflineRenderer.renderNotes for the same project (an arranged offline render is a LiveEngine render from 0).
/// Windows: a script with every action gives the same N and S lines, and the same C lines once ordered by tick, in
/// windows of 37, 240, 960 and 3840 ticks (renderCC lists a window voice by voice, so its order follows the windows).
func selftestMode(_ args: [String]) throws -> Int32 {
    var failed = 0
    for seed: UInt64 in [Project.fullEnsemble.masterSeed, 0xBE12_71A9, 1, 0x0123_4567_89AB_CDEF] {
        let bars = 800
        var project = Project.fullEnsemble
        project.key = Key(root: 9, scale: .preset(.naturalMinor))
        project.masterSeed = seed
        project.arrangement = .classicArc
        let expected = OfflineRenderer.renderNotes(project: project, bars: bars)
        let script = "template full\nseed 0x\(String(seed, radix: 16))\narranger classic\nwindow 3840\nbars \(bars)\n"
        var run = Run(try Script(text: script))
        var got: [RenderedNote] = []
        var stop = "no stop"
        run.play(emit: { if $0.hasPrefix("S") { stop = "stop at \($0.dropFirst(2))" } }, onNote: { got.append($0) })
        let same = got == expected
        print("\(same ? "ok  " : "FAIL") parity full classic seed \(seed): \(got.count) notes,"
              + " offline \(expected.count), \(stop)")
        if !same { failed += 1 }
    }
    var first: [String]? = nil
    for window in [37, 240, 960, 3840] {
        var run = Run(try Script(text: "window \(window)\n" + exerciseScript))
        var lines: [String] = []
        run.play(emit: { line in if !line.hasPrefix("H") { lines.append(line) } })
        let ccs = lines.enumerated().filter { $0.element.hasPrefix("C") }
            .map { (tick: Int($0.element.split(separator: " ")[1])!, index: $0.offset, line: $0.element) }
            .sorted { $0.tick != $1.tick ? $0.tick < $1.tick : $0.index < $1.index }.map(\.line)
        let streams = lines.filter { $0.hasPrefix("N") } + ccs + lines.filter { $0.hasPrefix("S") }
        let same = !streams.isEmpty && (first == nil || streams == first!)
        print("\(same ? "ok  " : "FAIL") windows of \(window): \(lines.count) lines")
        if !same { failed += 1 }
        if first == nil { first = streams }
    }
    print(failed == 0 ? "selftest ok" : "selftest: \(failed) failed")
    return failed == 0 ? 0 : 1
}

/// Every action, at ticks on and off bar lines, two in one bar, a reseed and a template load.
let exerciseScript = """
    template demo
    seed 0xBE1271A9
    bars 96
    at 7680 transpose 4
    at 10000 regenerate bedPluck1
    at 12345 save 0
    at 15360 lock bass 1
    at 15360 reroll
    at 20001 blank bedPluck2
    at 23040 key 2 dorian
    at 26000 density 35
    at 30000 bpart
    at 30001 bpart
    at 34567 tension
    at 38400 recall 0
    at 42000 voice bedPluck2 off
    at 46080 voice bedPluck2 on
    at 50000 density 100
    at 53760 key 9 naturalMinor
    at 57600 arranger on
    at 65000 advance
    at 70000 reseed 0x1234
    at 72000 arranger off
    at 80001 load ambient 0x0A3B11ED
    at 81000 tension
    at 92000 scene 1
    at 93000 lock drone 1
    at 93001 reroll

    """

/// stdout in large writes (a script can print a million lines).
struct Output {
    private var buffer = ""

    mutating func line(_ text: String) {
        buffer += text
        buffer += "\n"
        if buffer.utf8.count >= 1 << 16 { flush() }
    }

    mutating func flush() {
        FileHandle.standardOutput.write(Data(buffer.utf8))
        buffer = ""
    }
}

let args = Array(CommandLine.arguments.dropFirst())
guard let name = args.first, let mode = modes.first(where: { $0.name == name }) else {
    let usage = modes.map { "  RefTool \($0.usage)" }.joined(separator: "\n")
    FileHandle.standardError.write(Data("usage:\n\(usage)\n".utf8))
    exit(2)
}
do {
    exit(try mode.run(Array(args.dropFirst())))
} catch {
    FileHandle.standardError.write(Data("RefTool \(name): \(error)\n".utf8))
    exit(2)
}
