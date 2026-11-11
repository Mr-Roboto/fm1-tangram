#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Focused host Composer live-form regression: no firmware flashing.
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-cc}
OUT=build/tests/composer-form
mkdir -p "$OUT"
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -Wno-implicit-fallthrough \
    ${COMPOSER_FORM_TEST_FLAGS:-} -Iapp/engine -Iapp/engine/softfloat -Itests/host \
    tests/host/composer_form_test.c app/engine/*.c app/engine/softfloat/*.c -lm \
    -o "$OUT/composer_form_test"
"$OUT/composer_form_test"
