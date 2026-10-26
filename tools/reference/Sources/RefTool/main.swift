// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Jiří Píša
// RefTool: the reference writer over Berlinan's pinned engine. One function per mode, listed in `modes`; a later task
// adds its mode (rng, gen, mut, harmony, arrange, cc, midi) as one more function and one more line there.
//   RefTool script FILE    the reference lines of a script (tests/equiv/README.md)
//   RefTool selftest       the tool checked against the engine's own offline renderer

import BerlinanEngine
import Foundation

let modes: [(name: String, usage: String, run: ([String]) throws -> Int32)] = [
    ("script", "script FILE", scriptMode),
    ("selftest", "selftest", selftestMode),
]

func scriptMode(_ args: [String]) throws -> Int32 {
    guard args.count == 1 else { throw ScriptError(description: "usage: RefTool script FILE") }
    let text = try String(contentsOfFile: args[0], encoding: .utf8)
    var run = Run(try Script(text: text))
    var out = Output()
    run.play(emit: { out.line($0) })
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
        print("\(same ? "ok  " : "FAIL") parity full classic seed \(seed): \(got.count) notes, offline \(expected.count),"
              + " \(stop)")
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
