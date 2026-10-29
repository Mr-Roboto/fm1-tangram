#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# TGM-001A host tests only; no firmware, simulator build, or legacy records.
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-cc}
OUT=build/tests/scales
mkdir -p "$OUT"
# Optional sanitizer flags apply to both compilation and linking.
$CC -O2 -std=c99 -Wall -Wextra -Werror ${SCALE_TEST_FLAGS:-} \
    -Iapp/engine -Itests/host tests/host/scale_test.c app/engine/music.c -o "$OUT/scale_test"
"$OUT/scale_test"
# ui.c is normally followed by screen.c in a unity build. This fixture replaces
# rendering with stubs; helpers used only by screen.c are intentionally unused.
# Vendored SoftFloat has an intentional fallthrough unannotated for GCC.
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -Wno-unused-function \
    -Wno-implicit-fallthrough \
    -DPLAT_HOST ${SCALE_TEST_FLAGS:-} -Iapp -Iapp/engine -Iapp/engine/softfloat \
    -Ibase/include -Itests/host tests/host/scale_app_test.c app/engine/*.c \
    app/engine/softfloat/*.c -lm -o "$OUT/scale_app_test"
if ! "$OUT/scale_app_test" > "$OUT/scale_app.log"; then
    cat "$OUT/scale_app.log"
    exit 1
fi
tail -n 1 "$OUT/scale_app.log"
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -Wno-implicit-fallthrough ${SCALE_TEST_FLAGS:-} \
    -Iapp/engine -Iapp/engine/softfloat -Itests/host tests/host/engine_test.c \
    tests/host/script.c app/engine/*.c app/engine/softfloat/*.c -lm -o "$OUT/music_regression"
"$OUT/music_regression" --music-only
