/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The firmware's <stdlib.h> (fm1.toml device_include: the JieLi toolchain is freestanding and has none), for the
 * engine's units: abs, the app's (libc.c). */
#pragma once

int abs(int v);
