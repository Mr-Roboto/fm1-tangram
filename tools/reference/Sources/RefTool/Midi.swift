// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Jiří Píša
// The midi mode: what Berlinan's Transport sends for a script (tests/equiv/README.md, "The MIDI reference"), through
// the runtime's own conversion (EventConversion.swift, MIDI1Event.swift, compiled unchanged): MIDI Start at tick 0,
// then per window NoteEventConverter.events of its notes (muted voices dropped, ratchets expanded, the offs at
// at + gate), ccEvents of its CC (never muted) and the clock pulses of clockTicks, all in a stable sort by tick, as
// the events reach the wire. One `tick status d1 d2` line each, the bytes of the event's UMP word.

import BerlinanEngine
import RuntimePure

func midiMode(_ args: [String]) throws -> Int32 {
    guard args.count == 1 else { throw ScriptError(description: "usage: RefTool midi FILE") }
    let script = try Script(text: try String(contentsOfFile: args[0], encoding: .utf8))
    // A loaded project and the arranger's ending are the runtime's own (stop_flush.bln): not in this reference.
    for entry in script.actions {
        if case .load = entry.action { throw ScriptError(description: "the midi mode plays no 'load'") }
    }
    var events: [(tick: Tick, event: MIDI1Event)] = [(tick: 0, event: .start)]   // Transport: Start at tick 0 (INT)
    var muted = Set<String>()
    var next = 0
    var stopped = false
    var run = Run(script)
    run.play(emit: { line in if line.hasPrefix("S") { stopped = true } }, onWindow: { window, notes, ccs, project in
        // Transport.setMuted for the windows that start at or after the mute's tick.
        while next < script.mutes.count && script.mutes[next].tick <= window.lowerBound {
            let m = script.mutes[next]
            if m.on { muted.insert(m.voice) } else { muted.remove(m.voice) }
            next += 1
        }
        let channels = Dictionary(uniqueKeysWithValues: project.voices.map { ($0.id, $0.channel) })
        for e in NoteEventConverter.events(for: notes, channelMap: channels, muted: muted) {
            events.append((tick: e.tick, event: e.event))
        }
        for e in NoteEventConverter.ccEvents(for: ccs, channelMap: channels) {
            events.append((tick: e.tick, event: e.event))
        }
        for tick in NoteEventConverter.clockTicks(in: window) { events.append((tick: tick, event: .clock)) }
    })
    if stopped { throw ScriptError(description: "the midi mode plays no arranger ending") }
    let sorted = events.enumerated()
        .sorted { $0.element.tick != $1.element.tick ? $0.element.tick < $1.element.tick : $0.offset < $1.offset }
    var out = Output()
    for (_, e) in sorted {
        let word = e.event.umpWord
        out.line("\(e.tick) \(hex2(word >> 16)) \(hex2(word >> 8)) \(hex2(word))")
    }
    out.flush()
    return 0
}

/// The low byte, two upper-case hex digits.
private func hex2(_ v: UInt32) -> String {
    let s = String(v & 0xFF, radix: 16, uppercase: true)
    return s.count == 1 ? "0" + s : s
}
