#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-cc}
OUT=build/tests/motif-evolution
mkdir -p "$OUT"
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -Wno-implicit-fallthrough \
    ${EVOLUTION_TEST_FLAGS:-} -Iapp/engine -Iapp/engine/softfloat -Itests/host \
    tests/host/motif_evolution_test.c app/engine/*.c app/engine/softfloat/*.c -lm -o "$OUT/evolution_test"
"$OUT/evolution_test"
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -Wno-implicit-fallthrough \
    -DPLAT_HOST ${EVOLUTION_TEST_FLAGS:-} -Iapp -Iapp/engine -Iapp/engine/softfloat -Ibase/include -Itests/host \
    tests/host/motif_runtime_test.c app/engine/*.c app/engine/softfloat/*.c -lm -o "$OUT/runtime_test"
"$OUT/runtime_test"
MOTIF_TEST_FLAGS=${EVOLUTION_TEST_FLAGS:-} sh tests/host/run_motif.sh
