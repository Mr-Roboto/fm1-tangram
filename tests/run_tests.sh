#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Jiří Píša
# Berlinan's tests: the engine (tests/host/engine_test.c: f64 against the host's IEEE binary64, the sine, and the
# parts against RefTool's vectors; the states packed for the flash), the saved project (tests/host/project_test.c:
# app/project.c on stubs of the platform and the transport), the live engine against Berlinan's on every script
# (tests/host/equiv_test.c), the
# firmware's own libc (tests/host/libc_test.c), the firmware free of float and double, the update path against
# Berlinan's package (./build.sh first), the whole app in fm1-base's simulator (tests/scenarios/*.bln: the engine,
# the panel, the clock, the saved state), with screenshots in app/build/scenarios/. fm1-base's own tests:
# base/tests/run_tests.sh.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"   # the package's SDK files, as ./build.sh
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
    -Itests/host -o $OUT/engine_test tests/host/engine_test.c tests/host/script.c app/engine/*.c \
    app/engine/softfloat/*.c -lm && $OUT/engine_test"
run project sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -DPLAT_HOST -Iapp/engine \
    -Iapp/engine/softfloat -Iapp -Ibase/include -Itests/host -o $OUT/project_test tests/host/project_test.c \
    app/engine/*.c app/engine/softfloat/*.c -lm && $OUT/project_test"
run equiv sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -Iapp/engine -Iapp/engine/softfloat \
    -Itests/host -o $OUT/equiv_test tests/host/equiv_test.c tests/host/script.c app/engine/*.c \
    app/engine/softfloat/*.c -lm && $OUT/equiv_test"
run libc sh -c "$CC -O2 -std=c99 -Wall -Wextra -Werror -Itests/host -o $OUT/libc_test tests/host/libc_test.c && \
    $OUT/libc_test"
# comments included ("IEEE binary64" in prose); the vendored SoftFloat (app/engine/softfloat/, but for platform.h, which is
# ours) keeps its upstream comments
# (grep's status 1 is "no match"; 2 is an error, such as a glob matching no file, and must not pass)
run no-double sh -c "grep -nwE 'double|float' app/*.c app/*.h app/engine/*.[ch] app/engine/softfloat/platform.h \
    app/libc/*.[ch]; [ \$? -eq 1 ]"
if [ -f $B/berlinan.fwsc ]; then
    run ota-entry sh -c "$CC -w -o $OUT/ota_test base/tests/ota_test.c && $OUT/ota_test $B/berlinan.fwsc"
    run update-loader sh -c "head -c 60000 $B/berlinan.bin > $OUT/old_app.bin && \
        $PY base/tools/fm1pkg_make.py $OUT/old_app.bin $B/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -w -o $OUT/ldr_test base/tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc $B/berlinan.fwsc"
else
    echo "  skip update path (no $B/berlinan.fwsc: ./build.sh)"
fi
run host-build sh build.sh --host
for s in tests/scenarios/*.bln; do
    n=$(basename "$s" .bln)
    mkdir -p "$B/scenarios/$n"
    run "scenario-$n" $B/host/berlinan_host "$s" "$B/scenarios/$n"
done
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
