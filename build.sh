#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Jiří Píša
# Build Berlinan with fm1-base (base/, a git submodule).
#   ./build.sh [--release X.Y] [--host | --web]
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain)
#   AC79_SDK         JieLi AC79 SDK checkout (default: ~/fw-AC79_AIoT_SDK)
#   PYTHON           a Python with Pillow
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
PY="${PYTHON:-python3}"
[ -f base/tools/build.py ] || { echo "build.sh: no fm1-base in base/ (git submodule update --init --recursive)"; exit 1; }
"$PY" -c 'import PIL' 2>/dev/null || { echo "build.sh: $PY has no Pillow (pip3 install Pillow)"; exit 1; }
# The vendor host builder accepts CC as one executable, without a host CFLAGS hook.
for arg in "$@"; do
    if [ "$arg" = "--host" ]; then
        export TANGRAM_HOST_CC="${CC:-cc}"
        export CC="$PWD/tools/host-cc.sh"
        break
    fi
done
exec "$PY" base/tools/build.py app "$@"
