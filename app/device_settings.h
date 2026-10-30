/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#include "plat.h"

enum { DEVICE_SYNC_INTERNAL, DEVICE_SYNC_EXTERNAL };
enum {
    DEVICE_CLOCK_USB = 1u << PORT_USB,
    DEVICE_CLOCK_TRS = 1u << PORT_TRS,
    DEVICE_CLOCK_BOTH = DEVICE_CLOCK_USB | DEVICE_CLOCK_TRS
};
typedef struct {
    uint8_t sync, ext_ports;
} device_settings_t;

/* Main-loop API. GLOBAL1 stores settings only; it never changes the transport.
 * init resets RAM and loads the record: 0 valid, 1 missing/invalid -> INT/BOTH.
 * set rejects invalid values (-1); an accepted edit (0) is saved after settling.
 * status/poll: 0 clean, 1 pending, negative platform save error (latched).
 * poll writes only after 250 ms without edits and rt_idle(). A failed save
 * stays pending without automatic retries; retry explicitly rearms it.
 * Returning to the last saved values cancels an unnecessary write. */
int device_settings_init(void);
device_settings_t device_settings_get(void);
int device_settings_set(device_settings_t settings);
int device_settings_status(void);
int device_settings_poll(void);
void device_settings_retry(void);
