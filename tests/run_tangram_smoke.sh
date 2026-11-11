#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Tangram BLE MIDI focused smoke gate. Intentionally separate from obsolete
# Berlinan-wide tests/run_tests.sh and its historical project/slot format oracles.
set -eu
cd "$(dirname "$0")/.."

printf '%s\n' '=== Global settings regression ==='
sh tests/host/run_device_settings.sh

printf '%s\n' '=== FM-Tangram host simulator build ==='
./build.sh --host

for test_name in ble_global ble_routing; do
    output_dir="app/build/scenarios/$test_name"
    mkdir -p "$output_dir"
    log_file="$output_dir/run.log"
    printf '\n=== Simulator: %s ===\n' "$test_name"
    if ./app/build/host/tangram_host "tests/scenarios/$test_name.bln" "$output_dir" >"$log_file" 2>&1; then
        cat "$log_file"
    else
        cat "$log_file"
        printf '\nFAILED: %s (see %s)\n' "$test_name" "$log_file" >&2
        exit 1
    fi
done

printf '\n%s\n' 'Tangram BLE smoke tests passed (host simulation only).'
