// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Jiří Píša
// The script language of tests/equiv/README.md and its run: a project as the Mac app instantiates it, rendered in
// consecutive windows, with each action played as the app plays it (UIState, AppServices) through a transport that
// works like Berlinan's (Transport.swift: actions bind at the render head, a loaded project takes over at the next bar
// line). The app-level project (AppServices.project) is kept apart from the running engine's, as on the Mac.

import BerlinanEngine

/// The three built-in templates, each in TemplateStore.defaultKey (A natural minor) as `inDefaultKey` puts them.
enum Template: String, CaseIterable {
    case demo, full, ambient

    static let defaultKey = Key(root: 9, scale: .preset(.naturalMinor))

    var project: Project {
        var p: Project
        switch self {
        case .demo: p = .demo
        case .full: p = .fullEnsemble
        case .ambient: p = .ambientDrift
        }
        p.key = Template.defaultKey
        return p
    }
}

/// `arranger off|classic|slow|ambient` before play: AppServices.setArrangerEnabled(false) or setArrangerForm(preset).
enum ArrangerSetting: String {
    case off, classic, slow, ambient
}

enum ScriptAction {
    case transpose(Int)
    case regenerate(String)
    case reroll
    case blank(String)
    case lock(String, Bool)
    case save(Int)
    case recall(Int)
    case key(Int, ModeID)
    case density(Int)
    case bpart
    case voice(String, Bool)
    case tension
    case scene(Int)
    case arranger(Bool)
    case advance
    case reseed(UInt64)
    case load(Template, UInt64)
}

struct ScriptError: Error, CustomStringConvertible {
    let description: String
}

struct Script {
    static let slots = 32
    /// Every template is 4/4 at 960 PPQN.
    static let barTicks = Meter.fourFour.ticksPerBar
    static let voiceIDs = Set(Template.allCases.flatMap { $0.project.voices.map(\.id) })

    var template: Template = .demo
    var seed: UInt64? = nil
    var arranger: ArrangerSetting? = nil
    var density: Int? = nil
    var window: Tick = 0
    var bars = 0
    var actions: [(tick: Tick, action: ScriptAction)] = []
    /// `mute V TICK 0|1`: the runtime's mute (Transport.setMuted), for windows that start at or after TICK. The
    /// engine never sees it: the script mode ignores it, the midi mode (Midi.swift) applies it.
    var mutes: [(voice: String, tick: Tick, on: Bool)] = []

    /// The project the app holds before play: the template, then the script's seed, arranger and density.
    var initialProject: Project {
        var p = template.project
        if let seed { p.masterSeed = seed }
        switch arranger {
        case nil: break
        case .off?:
            var config = p.arrangement ?? ArrangementConfig()
            config.enabled = false
            p.arrangement = config
        case .classic?: p.arrangement = .classicArc
        case .slow?: p.arrangement = .slowBuild
        case .ambient?: p.arrangement = .ambientPlateau
        }
        if let density { p.density = max(0, min(1, Double(density) / 100)) }
        return p
    }

    init(text: String) throws {
        var seen = Set<String>()
        var lastTick: Tick = 0
        var lastMute: Tick = 0
        for (index, raw) in text.split(separator: "\n", omittingEmptySubsequences: false).enumerated() {
            let line = index + 1
            let body = raw.split(separator: "#", maxSplits: 1, omittingEmptySubsequences: false).first ?? ""
            let words = body.split(whereSeparator: { $0 == " " || $0 == "\t" || $0 == "\r" }).map(String.init)
            guard let head = words.first else { continue }
            func fail(_ why: String) -> ScriptError { ScriptError(description: "line \(line): \(why)") }
            if head == "at" {
                guard words.count >= 3, let tick = Script.integer(words[1]), tick >= 0 else {
                    throw fail("expected 'at TICK ACTION...'")
                }
                guard Tick(tick) >= lastTick else { throw fail("ticks must not decrease") }
                lastTick = Tick(tick)
                let parsed: ScriptAction?
                do { parsed = try Script.action(Array(words.dropFirst(2))) } catch { throw fail("\(error)") }
                guard let action = parsed else {
                    throw fail("bad action '\(words.dropFirst(2).joined(separator: " "))'")
                }
                actions.append((tick: Tick(tick), action: action))
                continue
            }
            if head == "mute" {
                guard words.count == 4, let tick = Script.integer(words[2]), tick >= 0,
                      words[3] == "0" || words[3] == "1" else { throw fail("expected 'mute V TICK 0|1'") }
                guard Script.voiceIDs.contains(words[1]) else { throw fail("unknown voice '\(words[1])'") }
                guard Tick(tick) >= lastMute else { throw fail("mute ticks must not decrease") }
                lastMute = Tick(tick)
                mutes.append((voice: words[1], tick: Tick(tick), on: words[3] == "1"))
                continue
            }
            guard words.count == 2 else { throw fail("expected '\(head) VALUE'") }
            guard seen.insert(head).inserted else { throw fail("'\(head)' given twice") }
            let value = words[1]
            switch head {
            case "template":
                guard let t = Template(rawValue: value) else { throw fail("unknown template '\(value)'") }
                template = t
            case "seed":
                guard let s = Script.seed(value) else { throw fail("seed must be 0x and 1-16 hex digits") }
                seed = s
            case "arranger":
                guard let a = ArrangerSetting(rawValue: value) else { throw fail("arranger off|classic|slow|ambient") }
                arranger = a
            case "density":
                guard let k = Script.integer(value), (0...100).contains(k) else { throw fail("density 0-100") }
                density = k
            case "window":
                guard let w = Script.integer(value), w >= 1 else { throw fail("window must be at least 1") }
                window = Tick(w)
            case "bars":
                guard let b = Script.integer(value), b >= 1 else { throw fail("bars must be at least 1") }
                bars = b
            default:
                throw fail("unknown line '\(head)'")
            }
        }
        for required in ["template", "window", "bars"] where !seen.contains(required) {
            throw ScriptError(description: "no '\(required)' line")
        }
        if let last = actions.last, last.tick >= Tick(bars) * Script.barTicks {
            throw ScriptError(description: "action at \(last.tick) is not before the end (\(bars) bars)")
        }
    }

    /// A decimal integer: an optional '-' and digits.
    static func integer(_ word: String) -> Int? {
        let digits = word.hasPrefix("-") ? word.dropFirst() : Substring(word)
        guard !digits.isEmpty, digits.allSatisfy({ $0 >= "0" && $0 <= "9" }) else { return nil }
        return Int(word)
    }

    static func seed(_ word: String) -> UInt64? {
        guard word.hasPrefix("0x") || word.hasPrefix("0X") else { return nil }
        let digits = word.dropFirst(2)
        guard (1...16).contains(digits.count), digits.allSatisfy(\.isHexDigit) else { return nil }
        return UInt64(digits, radix: 16)
    }

    private static func action(_ w: [String]) throws -> ScriptAction? {
        func voice(_ word: String) throws -> String {
            guard voiceIDs.contains(word) else { throw ScriptError(description: "unknown voice '\(word)'") }
            return word
        }
        func onOff(_ word: String) -> Bool? { word == "on" ? true : word == "off" ? false : nil }
        switch (w[0], w.count) {
        case ("transpose", 2): return integer(w[1]).map { .transpose($0) }
        case ("regenerate", 2): return .regenerate(try voice(w[1]))
        case ("reroll", 1): return .reroll
        case ("blank", 2): return .blank(try voice(w[1]))
        case ("lock", 3):
            guard w[2] == "0" || w[2] == "1" else { return nil }
            return .lock(try voice(w[1]), w[2] == "1")
        case ("save", 2), ("recall", 2):
            guard let slot = integer(w[1]), (0..<slots).contains(slot) else { return nil }
            return w[0] == "save" ? .save(slot) : .recall(slot)
        case ("key", 3):
            guard let root = integer(w[1]), (0...11).contains(root),
                  let mode = ModeID(rawValue: w[2]), mode != .custom else { return nil }
            return .key(root, mode)
        case ("density", 2):
            guard let k = integer(w[1]), (0...100).contains(k) else { return nil }
            return .density(k)
        case ("bpart", 1): return .bpart
        case ("voice", 3):
            let id = try voice(w[1])
            return onOff(w[2]).map { .voice(id, $0) }
        case ("tension", 1): return .tension
        case ("scene", 2):
            guard let n = integer(w[1]), n >= 0 else { return nil }
            return .scene(n)
        case ("arranger", 2): return onOff(w[1]).map { .arranger($0) }
        case ("advance", 1): return .advance
        case ("reseed", 2): return seed(w[1]).map { .reseed($0) }
        case ("load", 3):
            guard let t = Template(rawValue: w[1]), let s = seed(w[2]) else { return nil }
            return .load(t, s)
        default: return nil
        }
    }
}

/// One play of a script. `emit` receives the reference lines; `onNote` every rendered note (transport ticks);
/// `onWindow` each window as the transport sends it on (transport ticks): its range, its notes and CC, and the project
/// of the engine that rendered it (Transport.project, whose voices give the channel map).
struct Run {
    private let script: Script
    /// AppServices.project: what the app holds; key, density, arranger, reseed and load change it.
    private var app: Project
    /// The running engine (Transport's scheduler loop) and where its tick 0 sits in transport ticks.
    private var engine: LiveEngine
    private var running: Project
    private var origin: Tick = 0
    private var barTicks: Tick
    /// Transport.pendingProject and Transport.pendingActions: consumed at the next wake (the next window).
    private var pendingProject: Project? = nil
    private var pendingActions: [LiveEngine.Action] = []
    /// UIState.snapshotSlots.
    private var slots: [LiveEngine.StateSnapshot?] = Array(repeating: nil, count: Script.slots)
    private var stopShown = false
    /// `script --text`: the canonical state text (each line after `T `) in place of each H line.
    var showText = false

    init(_ script: Script) {
        self.script = script
        app = script.initialProject
        engine = LiveEngine(project: app)
        running = app
        barTicks = app.meter.ticksPerBar
    }

    /// 16 bars of 4/4: an H line follows every window whose end crosses a multiple of this.
    static let hashPeriod = 16 * Script.barTicks

    mutating func play(emit: (String) -> Void, onNote: (RenderedNote) -> Void = { _ in },
                       onWindow: (Range<Tick>, [RenderedNote], [RenderedCC], Project) -> Void = { _, _, _, _ in }) {
        let total = Tick(script.bars) * Script.barTicks
        let actions = script.actions
        var next = 0
        var head: Tick = 0
        while head < total {
            // The actions at this tick, in script order, between two wakes.
            while next < actions.count && actions[next].tick == head {
                apply(actions[next].action)
                next += 1
            }
            // The wake: a pending project takes over on its bar line, then the actions bind at the render head.
            var projectBoundary: Tick? = nil
            if let project = pendingProject {
                let boundary = origin + (head - origin + barTicks - 1) / barTicks * barTicks
                if head == boundary {
                    engine = LiveEngine(project: project)
                    running = project
                    origin = boundary
                    barTicks = project.meter.ticksPerBar
                    pendingProject = nil
                    stopShown = false
                } else {
                    projectBoundary = boundary
                }
            }
            for action in pendingActions { engine.enqueue(action, atTick: head - origin) }
            pendingActions.removeAll()
            // The window: up to the next multiple of `window`, the next action's tick, the project boundary, the end.
            var end = min((head / script.window + 1) * script.window, total)
            if next < actions.count { end = min(end, actions[next].tick) }
            if let boundary = projectBoundary { end = min(end, boundary) }
            let local = (head - origin)..<(end - origin)
            var notes: [RenderedNote] = []
            for note in engine.render(range: local) {
                emit(Dump.noteLine(note, origin: origin))
                var shifted = note
                shifted.at += origin
                onNote(shifted)
                notes.append(shifted)
            }
            var ccs: [RenderedCC] = []
            for cc in engine.renderCC(range: local) {
                emit(Dump.ccLine(cc, origin: origin))
                var shifted = cc
                shifted.at += origin
                ccs.append(shifted)
            }
            onWindow(head..<end, notes, ccs, running)
            if !stopShown, let stop = engine.pendingStopTick {
                emit(Dump.stopLine(stop + origin))
                stopShown = true
            }
            if end / Run.hashPeriod > head / Run.hashPeriod {
                if showText {
                    for line in Dump.stateText(engine).split(separator: "\n") { emit("T \(line)") }
                } else {
                    emit(Dump.hashLine(end, engine))
                }
            }
            head = end
            // The arranger's ending: the transport stops once the render head reaches the stop tick.
            if let stop = engine.pendingStopTick, head >= stop + origin { break }
        }
    }

    /// One action as the app performs it. Reads of the engine see the state of the last window, as the Mac's
    /// mirror does (Transport publishes it after every wake); engine actions wait for the next wake.
    private mutating func apply(_ action: ScriptAction) {
        switch action {
        case .transpose(let offset):                      // UIState.pressDegree
            pendingActions.append(.transpose(degreeOffset: offset))
        case .regenerate(let id):
            pendingActions.append(.regenerate(voiceID: id))
        case .reroll:                                     // UIState.rerollAllUnlocked
            for voice in engine.snapshot().voices where !voice.pattern.locked {
                pendingActions.append(.regenerate(voiceID: voice.id))
            }
        case .blank(let id):
            pendingActions.append(.blankCycle(voiceID: id))
        case .lock(let id, let on):
            pendingActions.append(.setLock(voiceID: id, on: on))
        case .save(let slot):                             // UIState.saveSnapshot: Transport.captureState()
            slots[slot] = engine.captureState()
        case .recall(let slot):                           // UIState.recallSnapshot: an empty slot does nothing
            if let state = slots[slot] { pendingActions.append(.recallSnapshot(state)) }
        case .key(let root, let mode):                    // AppServices.applyLiveMusicalChanges
            let key = Key(root: root, scale: .preset(mode))
            app.key = key
            pendingActions.append(.setKey(key))
        case .density(let k):                             // AppServices.setDensity while playing
            let value = max(0, min(1, Double(k) / 100))
            app.density = value
            pendingActions.append(.setDensity(value))
        case .bpart:
            pendingActions.append(.fireBPart)
        case .voice(let id, let on):
            pendingActions.append(.setVoiceActive(voiceID: id, on: on))
        case .tension:                                    // UIState.spendTension: the pad, else the first voice
            if let target = app.voices.first(where: { $0.role == .pad }) ?? app.voices.first {
                pendingActions.append(.spendTension(voiceID: target.id))
            }
        case .scene(let n):                               // UIState.morphToScene: the app project's n-th scene
            let scenes = app.ccScenes ?? []
            if n < scenes.count { pendingActions.append(.morphToScene(name: scenes[n].name, bars: 8)) }
        case .arranger(let on):                           // AppServices.setArrangerEnabled while playing
            var config = app.arrangement ?? ArrangementConfig()
            config.enabled = on
            app.arrangement = config
            pendingActions.append(.setArrangementEnabled(on))
        case .advance:
            pendingActions.append(.advancePhase)
        case .reseed(let seed):                           // AppServices.reseed while playing
            var project = app
            project.masterSeed = seed
            let fresh = LiveEngine(project: project).captureState()
            app = project
            pendingActions.append(.recallSnapshot(fresh))
        case .load(let template, let seed):               // AppServices.applyTemplate while playing
            var project = template.project
            project.masterSeed = seed
            app = project
            pendingProject = project                      // Transport.loadProject
        }
    }
}
