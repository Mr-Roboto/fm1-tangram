/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): Berlinan's checks for
 * "expect".
 *
 * Berlinan's checks: playing (fm1-base's own: store_writes clocks_out midi_out ...). */

int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "playing"))
        *got = playing;                  /* 0 until the transport (runtime.c) */
    else
        return 0;
    return 1;
}
