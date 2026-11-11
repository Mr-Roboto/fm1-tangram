#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Tangram 1.0 UX pilot regression on top of the frozen functional smoke gate.
set -eu
cd "$(dirname "$0")/.."

sh tests/run_tangram_smoke.sh

out="app/build/scenarios/ux_pending_help"
mkdir -p "$out"
printf '\n%s\n' '=== UX: help cards and staged parameter markers ==='
if ./app/build/host/tangram_host tests/scenarios/ux_pending_help.bln "$out" >"$out/run.log" 2>&1; then
    cat "$out/run.log"
else
    cat "$out/run.log"
    printf '\nFAILED UX regression (see %s/run.log)\n' "$out" >&2
    exit 1
fi
printf '\n%s\n' 'Tangram 1.0 UX pilot smoke tests passed (host simulation only).'
