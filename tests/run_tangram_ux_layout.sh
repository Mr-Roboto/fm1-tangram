#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Tangram UX layout phase 2: run frozen functional and phase-1 gates first.
set -eu
cd "$(dirname "$0")/.."
sh tests/run_tangram_ux.sh
out=app/build/scenarios/ux_layout_labels
mkdir -p "$out"
printf '\n%s\n' '=== UX layout phase 2: ENV VOICE rename ==='
if ./app/build/host/tangram_host tests/scenarios/ux_layout_labels.bln "$out" >"$out/run.log" 2>&1; then
    cat "$out/run.log"
else
    cat "$out/run.log"
    printf '\nFAILED ENV VOICE navigation/layout check (%s/run.log)\n' "$out" >&2
    exit 1
fi
printf '\n%s\n' 'Tangram 1.0 UX layout checks passed (host simulation only).'
