#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Standalone BLE/global-device-settings host regression.
# The legacy tests/host/project_test.c still targets historical musical records.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p app/build/tests
"${CC:-cc}" -O2 -std=c99 -Wall -Wextra -Werror -DPLAT_HOST \
    -Iapp -Iapp/engine -Ibase/include -Itests/host \
    -o app/build/tests/device_settings_test tests/host/device_settings_test.c
exec ./app/build/tests/device_settings_test
