#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Focused drum regression only; no chained suites.
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-cc}
OUT=build/tests/heartbeat
mkdir -p "$OUT"
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -Wno-implicit-fallthrough \
    ${HEARTBEAT_TEST_FLAGS:-} -Iapp/engine -Iapp/engine/softfloat -Itests/host \
    tests/host/heartbeat_test.c app/engine/*.c app/engine/softfloat/*.c -lm -o "$OUT/heartbeat_test"
"$OUT/heartbeat_test"
