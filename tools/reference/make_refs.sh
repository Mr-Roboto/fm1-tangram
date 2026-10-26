#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Jiří Píša
# Regenerate the committed reference files with RefTool over the pinned Berlinan engine: every tests/equiv/*.script
# into its .ref (script mode), then tests/equiv/<mode>.ref for each vector mode in VECTORS (templates, rng; the tasks
# that add gen, mut, harmony, arrange and cc add them here). The tests never run this; they read the committed
# files.
#   tools/reference/make_refs.sh
#   BERLINAN   Berlinan checkout, for fetch.sh (default: ~/Development/Claude/berlin_school_manual)
set -e
cd "$(dirname "$0")/../.."
VECTORS="templates rng"
SWIFT_PIN="Apple Swift version 6.3.3 "
swift --version 2>&1 | grep -q "$SWIFT_PIN" || { echo "make_refs.sh: the references are made with $SWIFT_PIN"; exit 1; }
sh tools/reference/fetch.sh
swift build -c release --package-path tools/reference
BIN="$(swift build -c release --package-path tools/reference --show-bin-path)/RefTool"
"$BIN" selftest
for s in tests/equiv/*.script; do
    r="${s%.script}.ref"
    "$BIN" script "$s" > "$r.tmp"
    mv "$r.tmp" "$r"
    echo "  $r"
done
for m in $VECTORS; do
    "$BIN" "$m" > "tests/equiv/$m.ref.tmp"
    mv "tests/equiv/$m.ref.tmp" "tests/equiv/$m.ref"
    echo "  tests/equiv/$m.ref"
done
