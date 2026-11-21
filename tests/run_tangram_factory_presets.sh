#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Focused 1.0 musical factory preset smoke; no firmware build or flash.
set -eu
cd "$(dirname "$0")/.."
./build.sh --host
out=app/build/scenarios/factory_presets_1_0
mkdir -p "$out"
if ./app/build/host/tangram_host tests/scenarios/factory_presets_1_0.bln "$out" >"$out/run.log" 2>&1; then
    cat "$out/run.log"
else
    cat "$out/run.log"
    printf '\nFAILED factory presets smoke (%s/run.log)\n' "$out" >&2
    exit 1
fi
printf '%s\n' 'Tangram factory preset host smoke passed.'
