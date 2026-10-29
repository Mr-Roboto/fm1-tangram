#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# TGM-001B: engine-only host checks, no firmware or hardware build.
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-cc}
OUT=build/tests/arp
mkdir -p "$OUT"
# SoftFloat's intentional fallthrough is not annotated for GCC.
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -Wno-implicit-fallthrough \
    ${ARP_TEST_FLAGS:-} -Iapp/engine -Iapp/engine/softfloat -Itests/host \
    tests/host/arp_test.c app/engine/*.c app/engine/softfloat/*.c -lm -o "$OUT/arp_test"
"$OUT/arp_test"
SCALE_TEST_FLAGS=${ARP_TEST_FLAGS:-} sh tests/host/run_scales.sh
# Reuse the engine binary built by run_scales for the complete host engine test,
# including arithmetic, original musical reference vectors and snapshots.
build/tests/scales/music_regression
