#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Focused selected-section regression only; no chained suites.
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-cc}
OUT=build/tests/composer-start
mkdir -p "$OUT"
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -Wno-implicit-fallthrough \
    ${COMPOSER_START_TEST_FLAGS:-} -Iapp/engine -Iapp/engine/softfloat -Itests/host \
    tests/host/composer_start_test.c app/engine/*.c app/engine/softfloat/*.c -lm -o "$OUT/composer_start_test"
"$OUT/composer_start_test"
