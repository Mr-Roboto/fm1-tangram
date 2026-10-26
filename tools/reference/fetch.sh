#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Jiří Píša
# Extract the pinned Berlinan engine sources into tools/reference/.berlinan/ (gitignored), unchanged, with git archive:
# nothing is written into the Berlinan checkout.
#   tools/reference/fetch.sh
#   BERLINAN   Berlinan checkout (default: ~/Development/Claude/berlin_school_manual)
set -e
cd "$(dirname "$0")"
BERLINAN="${BERLINAN:-$HOME/Development/Claude/berlin_school_manual}"
PIN=0e4a7195cefb9aed5f21ee46bd6eddeebeb776ac
git -C "$BERLINAN" cat-file -e "$PIN^{commit}" 2>/dev/null || { echo "fetch.sh: $BERLINAN has no commit $PIN"; exit 1; }
rm -rf .berlinan
mkdir -p .berlinan
git -C "$BERLINAN" archive "$PIN" Sources/BerlinanEngine Sources/BerlinanRuntime/EventConversion.swift \
    Sources/BerlinanRuntime/MIDI1Event.swift Sources/BerlinanRuntime/NoteRegistry.swift \
    Sources/Berlinan/TemplateStore.swift | tar -x -C .berlinan
echo "$PIN" > .berlinan/PIN
