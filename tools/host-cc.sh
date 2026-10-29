#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Expose POSIX clocks before system headers under the host builder's strict C99.
exec "${TANGRAM_HOST_CC:-cc}" -D_POSIX_C_SOURCE=200809L "$@"
