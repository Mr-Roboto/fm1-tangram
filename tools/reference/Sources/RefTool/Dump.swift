// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Jiří Píša
// The reference lines (N, C, S, H) and the state hash: FNV-1a 64 over a canonical text built only from the engine's
// public API (snapshot() and captureState()). tests/equiv/README.md defines both field by field; the C runner
// (Task 11) writes the same text from its own state.

import BerlinanEngine

enum Dump {

    /// `N <at> <voiceID> <midi> <vel> <gate> <accent> <ratchet>`; `at` in transport ticks (engine tick + origin).
    static func noteLine(_ n: RenderedNote, origin: Tick) -> String {
        "N \(n.at + origin) \(n.voiceID) \(n.midiNote) \(n.velocity) \(n.gate) \(flag(n.accent)) \(n.ratchet)"
    }

    /// `C <at> <voiceID> <controller> <value>`.
    static func ccLine(_ c: RenderedCC, origin: Tick) -> String {
        "C \(c.at + origin) \(c.voiceID) \(c.controller) \(c.value)"
    }

    static func stopLine(_ tick: Tick) -> String { "S \(tick)" }

    static func hashLine(_ tick: Tick, _ engine: LiveEngine) -> String {
        "H \(tick) \(hex16(fnv1a64(stateText(engine))))"
    }

    /// The canonical state text: one line per item, fields separated by one space, every line ending in "\n".
    static func stateText(_ engine: LiveEngine) -> String {
        let snap = engine.snapshot()
        let generations = engine.captureState().generations
        var out = ""
        for v in snap.voices {
            let p = v.pattern
            out += "voice \(v.id) \(flag(v.active)) \(flag(v.blankActive)) \(pending(v.pendingRequest))"
            out += " \(generations[v.id]!) \(p.length) \(p.startOffset) \(flag(p.locked)) \(lifecycle(p.lifecycle))"
            out += " \(p.provenance.seed)\n"
            for n in p.notes {
                out += "note \(n.position) \(n.pitch.degree) \(n.pitch.octave) \(n.pitch.accidental) \(n.velocity)"
                out += " \(n.gate) \(flag(n.accent)) \(n.ratchet)\n"
            }
        }
        for e in snap.scheduleEvents { out += "sched \(e.bar) \(e.degreeOffset)\n" }
        out += "key \(snap.key.root)" + snap.key.scale.offsets.map { " \($0)" }.joined() + "\n"
        out += "density \(hex16(snap.density.bitPattern))\n"
        out += "tension \(snap.tensionRemaining)\n"
        if let a = snap.arrangement {
            out += "arr \(a.phase.rawValue) \(a.barsInPhase) \(a.barsUntilChange)"
            out += a.activeVoices.map { " \($0)" }.joined() + "\n"
        } else {
            out += "arr -\n"
        }
        for u in snap.upcomingEvents {
            out += "up \(u.tick) \(u.kind.rawValue) \(u.detail.isEmpty ? "-" : u.detail) \(u.voiceID ?? "-")\n"
        }
        return out
    }

    static func fnv1a64(_ text: String) -> UInt64 {
        var h: UInt64 = 0xcbf2_9ce4_8422_2325
        for byte in text.utf8 {
            h ^= UInt64(byte)
            h = h &* 0x0000_0100_0000_01b3
        }
        return h
    }

    /// 16 lower-case hex digits.
    static func hex16(_ value: UInt64) -> String {
        let digits = String(value, radix: 16)
        return String(repeating: "0", count: 16 - digits.count) + digits
    }

    private static func flag(_ on: Bool) -> String { on ? "1" : "0" }

    private static func pending(_ request: LiveEngine.PendingRequest?) -> String {
        switch request {
        case nil: "-"
        case .blank(let tick)?: "blank \(tick)"
        case .regenerate(let tick)?: "regenerate \(tick)"
        case .replace(let tick)?: "replace \(tick)"
        case .melodyToggle(let tick)?: "melodyToggle \(tick)"
        }
    }

    private static func lifecycle(_ lifecycle: Lifecycle) -> String {
        switch lifecycle {
        case .repeating(let remaining): "\(remaining)"
        case .established: "E"
        }
    }
}
