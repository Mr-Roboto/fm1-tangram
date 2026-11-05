#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Focused runtime counters only; no chained suites.
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-cc}
OUT=build/tests/composer-counters
mkdir -p "$OUT"
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -Wno-implicit-fallthrough \
    -DPLAT_HOST ${COUNTERS_TEST_FLAGS:-} -Iapp -Iapp/engine -Iapp/engine/softfloat \
    -Ibase/include -Itests/host tests/host/composer_counters_test.c app/engine/*.c \
    app/engine/softfloat/*.c -lm -o "$OUT/composer_counters_test"
"$OUT/composer_counters_test"
