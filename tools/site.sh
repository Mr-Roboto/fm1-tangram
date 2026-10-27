#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Jiří Píša
# Build Berlinan's site for release VERSION in app/build/site (GitHub Pages, https://jiripisa.github.io/fm1-berlinan/):
# the release package (./build.sh --release VERSION), the emulator (./build.sh --web --release VERSION), fm1-base's
# site (base/tools/make_site.py: install/, emu/, words/, firmware/), then Berlinan's own pages over it
# (tools/site_pages.py: the front page, the manual and the installer, in English and in Czech; the links checked).
#   tools/site.sh [VERSION]       (0.5.1 if not given)
#   PYTHON   a Python with Pillow (as for ./build.sh); Emscripten (emcc) for the emulator
# Nothing is written to an FM-1: the site is only built. To look at it:
#   python3 -m http.server -d app/build/site 8000      then http://localhost:8000/
set -e
cd "$(dirname "$0")/.."
V="${1:-0.5.1}"
PYTHON="${PYTHON:-python3}"
export PYTHON
./build.sh --release "$V"
./build.sh --web --release "$V"
"$PYTHON" base/tools/make_site.py app "app/build/berlinan-$V.fwsc" "$V" app/build/site
"$PYTHON" tools/site_pages.py "$V" app/build/site
