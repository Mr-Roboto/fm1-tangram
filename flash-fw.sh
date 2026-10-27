#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Jiří Píša
# Install Berlinan on an FM-1 over USB (fm1-base's tools/fm1_install.py), with the Python that has mido.
#   ./flash-fw.sh [--build] [PACKAGE.fwsc] [fm1_install.py options, e.g. --yes --port NAME]
#   --build   build the package first (./build.sh)
#   FM1_PY    a Python with mido and python-rtmidi (default: ~/.venvs/fm1/bin/python)
#   PYTHON    the Python ./build.sh uses (a Python with Pillow), for --build
set -e
cd "$(dirname "$0")"
FM1_PY="${FM1_PY:-$HOME/.venvs/fm1/bin/python}"
PKG=app/build/berlinan.fwsc

if [ "${1:-}" = "--build" ]; then
    shift
    sh build.sh
fi
case "${1:-}" in
*.fwsc) PKG="$1"; shift ;;
esac

[ -x "$FM1_PY" ] || { echo "flash-fw.sh: no $FM1_PY (python3 -m venv ~/.venvs/fm1 && ~/.venvs/fm1/bin/pip install mido python-rtmidi)"; exit 1; }
"$FM1_PY" -c 'import mido, rtmidi' 2>/dev/null || { echo "flash-fw.sh: $FM1_PY has no mido / python-rtmidi"; exit 1; }
[ -f "$PKG" ] || { echo "flash-fw.sh: no $PKG (./flash-fw.sh --build, or ./build.sh)"; exit 1; }

echo "package: $PKG"
"$FM1_PY" base/tools/fm1_install.py --info "$@" || { echo "flash-fw.sh: no FM-1 found (USB data cable, FM-1 on)"; exit 3; }
exec "$FM1_PY" base/tools/fm1_install.py "$PKG" "$@"
