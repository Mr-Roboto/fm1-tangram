#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# TGM-002A: focused MOTIF plus ARP, scales and complete host engine regression.
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-cc}
OUT=build/tests/motif
mkdir -p "$OUT"
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -Wno-implicit-fallthrough \
    ${MOTIF_TEST_FLAGS:-} -Iapp/engine -Iapp/engine/softfloat -Itests/host \
    tests/host/motif_test.c app/engine/*.c app/engine/softfloat/*.c -lm -o "$OUT/motif_test"
"$OUT/motif_test"
ARP_TEST_FLAGS=${MOTIF_TEST_FLAGS:-} sh tests/host/run_arp.sh
