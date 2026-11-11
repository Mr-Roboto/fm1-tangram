#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Tangram 1.0 feature-exception branch (LFO + FX MIXER).
set -eu
cd "$(dirname "$0")/.."
sh tests/run_tangram_ux_layout.sh
out=app/build/scenarios/midi_controls_1_0
mkdir -p "$out"
printf '\n%s\n' '=== Tangram 1.0 editable LFO and FX mixer regression ==='
if ./app/build/host/tangram_host tests/scenarios/midi_controls_1_0.bln "$out" >"$out/run.log" 2>&1; then
    cat "$out/run.log"
else
    cat "$out/run.log"
    printf '\nFAILED MIDI controls regression (see %s/run.log)\n' "$out" >&2
    exit 1
fi
printf '\n%s\n' 'Tangram 1.0 MIDI controls smoke tests passed (host simulation only).'
