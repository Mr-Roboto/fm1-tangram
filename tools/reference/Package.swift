// swift-tools-version: 6.0
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Jiří Píša
// The reference tool's package: Berlinan's engine and three pure runtime files, compiled as fetch.sh extracted them
// from the pinned commit (never patched), and RefTool, which writes the reference files (tests/equiv/README.md).
import PackageDescription

let package = Package(
    name: "BerlinanReference",
    platforms: [.macOS(.v14)],
    targets: [
        .target(name: "BerlinanEngine", path: ".berlinan/Sources/BerlinanEngine"),
        .target(name: "RuntimePure", dependencies: ["BerlinanEngine"], path: ".berlinan/Sources/BerlinanRuntime",
                sources: ["EventConversion.swift", "MIDI1Event.swift", "NoteRegistry.swift"]),
        .executableTarget(name: "RefTool", dependencies: ["BerlinanEngine", "RuntimePure"], path: "Sources/RefTool"),
    ]
)
