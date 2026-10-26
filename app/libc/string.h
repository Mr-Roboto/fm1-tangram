/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* The firmware's <string.h> (fm1.toml device_include: the JieLi toolchain is freestanding and has none), for the
 * engine's units: memset, memcpy and memcmp are felucca-bsp's (chip/libc.c), memmove and strcmp the app's (libc.c). */
#pragma once
#include <stddef.h>

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
int strcmp(const char *a, const char *b);
