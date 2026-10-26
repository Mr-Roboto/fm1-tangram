/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* Berlinan in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): Berlinan's checks for
 * "expect", its commands and a reboot.
 *
 * Berlinan's checks: playing (fm1-base's own: store_writes clocks_out midi_out ...). */

#include <string.h>

int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "playing"))
        *got = playing;                  /* 0 until the transport (runtime.c) */
    else
        return 0;
    return 1;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)cmd, (void)a, (void)b, (void)c, (void)d;
    return 0;                            /* none yet */
}

void app_host_reboot(void) {}            /* nothing is kept in RAM yet */
