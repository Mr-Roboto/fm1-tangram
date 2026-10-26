#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Jiří Píša
# Berlinan's tests: the engine (tests/host/engine_test.c: f64 against the host's IEEE binary64, the sine), the
# firmware free of float and double, the whole app in fm1-base's simulator (tests/scenarios/*.bln: the panel, the
# clock, the saved state), with screenshots in app/build/scenarios/. fm1-base's own tests: base/tests/run_tests.sh.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
B=app/build
OUT=$B/tests
FAIL=0
mkdir -p "$OUT" $B/scenarios
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
run engine sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -Iapp/engine -Iapp/engine/softfloat \
    -o $OUT/engine_test tests/host/engine_test.c app/engine/*.c app/engine/softfloat/*.c -lm && $OUT/engine_test"
# comments included ("IEEE binary64" in prose); the vendored SoftFloat (app/engine/softfloat/, but for platform.h, which is
# ours) keeps its upstream comments
# (grep's status 1 is "no match"; 2 is an error, such as a glob matching no file, and must not pass)
run no-double sh -c "grep -nwE 'double|float' app/*.c app/*.h app/engine/*.[ch] app/engine/softfloat/platform.h; [ \$? -eq 1 ]"
run host-build sh build.sh --host
for s in tests/scenarios/*.bln; do
    n=$(basename "$s" .bln)
    mkdir -p "$B/scenarios/$n"
    run "scenario-$n" $B/host/berlinan_host "$s" "$B/scenarios/$n"
done
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
